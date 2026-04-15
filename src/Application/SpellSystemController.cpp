#include "SpellSystemController.h"

#include "Adapters/Outbound/EquipSlots.h"
#include "Adapters/Outbound/MagicEquip.h"
#include "Adapters/Outbound/RestoreEquip.h"
#include "Adapters/Outbound/SyntheticInput.h"
#include "Application/AssignService.h"
#include "Application/InputController.h"
#include "Config/ConfigAdapter.h"
#include "Domain/State.h"
#include "Input/HotkeyMatcher.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Shared/Hand.h"
#include "Shared/SlotPressAction.h"
#include "Shared/SlotPressResult.h"
#include "UI/HoveredForm.h"

namespace Application {

    SpellSystemController& SpellSystemController::Get() {
        static SpellSystemController inst;  // NOSONAR
        if (static bool initialized = false; !initialized) {
            inst.Initialize();
            initialized = true;
        }
        return inst;
    }

    void SpellSystemController::OnFrame(float dt, bool inputBlocked) const {
        DispatchSlotEvents();

        if (!inputBlocked) {
            IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
            IntegratedMagic::MagicState::Get().PumpAutomatic(dt);
        }
    }

    void SpellSystemController::DispatchSlotEvents() const {
        auto& input = InputController::Get();
        auto& state = IntegratedMagic::MagicState::Get();
        auto* player = RE::PlayerCharacter::GetSingleton();

        for (auto s = input.ConsumePressedSlot(); s.has_value(); s = input.ConsumePressedSlot()) {
            if (!RE::PlayerCharacter::GetSingleton()) continue;
            MAGIC_DEBUG_LOG("[SpellSystem] HandleSlotPressed: slot={}", *s);
            auto action = state.OnSlotPressed(*s);
            if (action.result == IntegratedMagic::SlotPressResult::Deactivated) input.SetSlotDeactivatedThisPress(*s);
            for (auto& intent : action.spellsToEquip)
                IntegratedMagic::MagicAction::EquipSpellInHand(player, intent.spell, intent.hand, action.skipAnim);
            if (action.shoutToEquip) IntegratedMagic::MagicAction::EquipShoutInVoice(player, action.shoutToEquip);
            if (action.startShoutDispatch) IntegratedMagic::detail::DispatchShout(1.0f, 0.0f);

            if (!action.spellsToEquip.empty()) state.OnEquipComplete(action.inventorySnapshotBefore);
        }

        for (auto s = input.ConsumeReleasedSlot(); s.has_value(); s = input.ConsumeReleasedSlot()) {
            MAGIC_DEBUG_LOG("[SpellSystem] HandleSlotReleased: slot={}", *s);
            state.OnSlotReleased(*s);
        }
    }

    void SpellSystemController::NotifyAnimEvent(std::string_view tag) const {
        using enum IntegratedMagic::Hand;
        auto& state = IntegratedMagic::MagicState::Get();

        if (tag == "EnableBumper"sv) {
            const auto r = state.NotifyAttackEnabled();

            if (auto* p = RE::PlayerCharacter::GetSingleton())
                IntegratedMagic::MagicAction::DisableSkipEquipVarsNow(p);

            if (r.dispatchLeft) IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
            if (r.dispatchRight) IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);
        }
        if (tag == "CastStop"sv || tag == "RitualSpellOut"sv) state.OnCastStop();
        if (tag == "InterruptCast"sv) state.OnCastInterrupt();
        if (tag == "BeginCastRight"sv) state.OnBeginCast(Right);
        if (tag == "BeginCastLeft"sv) state.OnBeginCast(Left);
        if (tag == "shoutStop"sv) state.OnShoutStop();
        if (tag == "blockStart"sv || tag == "BashExit"sv) {
            if (!state.IsPressMode()) state.ForceExit();
        }
        if (tag == "tailMTIdle"sv || tag == "IdleStop"sv) {
            if (state.IsWaitingSheatheRestore()) state.NotifySheatheComplete();
        }
        if (tag == "MRh_SpellFire_Event"sv) state.OnSpellFired(Right);
        if (tag == "MLh_SpellFire_Event"sv) state.OnSpellFired(Left);
    }

    void SpellSystemController::NotifyPlayerDeath() const { IntegratedMagic::MagicState::Get().ForceExit(); }

    void SpellSystemController::NotifyLoadGame() const { IntegratedMagic::MagicState::Get().ForceExit(); }

    void SpellSystemController::NotifyMenuOpen(std::string_view menuName) const {
        static constexpr std::array kInterruptMenus = {
            "ContainerMenu"sv, "InventoryMenu"sv, "MagicMenu"sv, "MapMenu"sv, "Journal Menu"sv, "Dialogue Menu"sv,
        };
        for (auto m : kInterruptMenus) {
            if (menuName == m) {
                IntegratedMagic::MagicState::Get().ForceExit();
                break;
            }
        }
    }

    void SpellSystemController::TryAssignHoveredToSlotByHotkey() const {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        if (static const RE::BSFixedString magicMenu{"MagicMenu"}; !ui->IsMenuOpen(magicMenu)) return;

        const auto type = IntegratedMagic::HoveredForm::GetHoveredMagicType();
        if (type == IntegratedMagic::HoveredForm::MagicType::None) return;

        const int n = InputController::Get().Slots().ActiveSlots();
        const auto& hotkeys = InputController::Get().Hotkeys();
        const auto& keys = InputController::Get().Keys();

        for (int slot = 0; slot < n; ++slot) {
            using enum IntegratedMagic::HoveredForm::MagicType;
            const auto& hk = hotkeys.slots[static_cast<std::size_t>(slot)];
            if (const bool comboDown =
                    Input::detail::ComboDown(hk.kb, keys.kbDown) || Input::detail::ComboDown(hk.gp, keys.gpDown);
                !comboDown)
                continue;

            if (type == Shout || type == Power)
                IntegratedMagic::MagicAssign::TryAssignHoveredShoutToSlot(slot);
            else if (type == RightOnlySpell)
                IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(slot, IntegratedMagic::Hand::Right);
            else
                IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(slot, IntegratedMagic::Hand::Left);
            break;
        }
    }

    void SpellSystemController::Initialize() {
        IntegratedMagic::Domain::OutboundDelegate d;

        d.dispatchAttack = [](IntegratedMagic::Hand hand, float value, float heldSecs) {
            IntegratedMagic::detail::DispatchAttack(hand, value, heldSecs);
        };
        d.dispatchShout = [](float value, float heldSecs) { IntegratedMagic::detail::DispatchShout(value, heldSecs); };
        d.disableSkipEquipVarsNow = []() {
            if (auto* p = RE::PlayerCharacter::GetSingleton())
                IntegratedMagic::MagicAction::DisableSkipEquipVarsNow(p);
        };
        d.applySkipEquipAnimReturn = []() {
            const bool skip = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
            if (auto* p = RE::PlayerCharacter::GetSingleton())
                IntegratedMagic::MagicAction::ApplySkipEquipAnimReturn(p, skip);
        };
        d.equipSpellInHand = [](RE::SpellItem* spell, IntegratedMagic::Hand hand) {
            const bool skip = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimation();
            if (auto* p = RE::PlayerCharacter::GetSingleton())
                IntegratedMagic::MagicAction::EquipSpellInHand(p, spell, hand, skip);
        };
        d.clearHandSpell = [](IntegratedMagic::Hand hand) {
            if (auto* p = RE::PlayerCharacter::GetSingleton()) IntegratedMagic::MagicAction::ClearHandSpell(p, hand);
        };
        d.clearHandSpellByRef = [](RE::SpellItem* spell, IntegratedMagic::Hand hand) {
            if (auto* p = RE::PlayerCharacter::GetSingleton())
                IntegratedMagic::MagicAction::ClearHandSpell(p, spell, hand);
        };
        d.equipShoutInVoice = [](RE::TESForm* form) {
            if (auto* p = RE::PlayerCharacter::GetSingleton()) IntegratedMagic::MagicAction::EquipShoutInVoice(p, form);
        };
        d.clearVoiceShout = []() {
            if (auto* p = RE::PlayerCharacter::GetSingleton()) IntegratedMagic::MagicAction::ClearVoiceShout(p);
        };
        d.reequipPrevExtraEquipped = [](RE::PlayerCharacter* player,
                                        std::vector<IntegratedMagic::ExtraEquippedItem>& items) {
            auto* mgr = RE::ActorEquipManager::GetSingleton();
            if (!mgr) return;
            auto idx = IntegratedMagic::BuildInventoryIndex(player);
            IntegratedMagic::Outbound::ReequipPrevExtraEquipped(player, mgr, idx, items);
        };
        d.getHandEquipSlot = [](IntegratedMagic::Hand hand) {
            return IntegratedMagic::EquipUtil::GetHandEquipSlot(hand);
        };
        d.restoreOneHand = [](bool leftHand, const IntegratedMagic::InventoryIndex& idx,
                              const IntegratedMagic::ObjSnapshot& want, const RE::BGSEquipSlot* slot) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* mgr = RE::ActorEquipManager::GetSingleton();
            if (player && mgr) IntegratedMagic::Outbound::RestoreOneHand(player, mgr, idx, leftHand, want, slot);
        };

        IntegratedMagic::MagicState::Get().SetOutboundDelegate(d);
    }

    void SpellSystemController::OnConfigChanged() const { InputController::Get().OnConfigChanged(); }
    void SpellSystemController::NotifyForeignEquip() const { IntegratedMagic::MagicState::Get().ForceExitNoRestore(); }
    bool SpellSystemController::IsSpellSystemActive() const { return IntegratedMagic::MagicState::Get().IsActive(); }
    int SpellSystemController::ActiveSlot() const { return IntegratedMagic::MagicState::Get().ActiveSlot(); }
    bool SpellSystemController::IsInSlotSetup() const { return IntegratedMagic::MagicState::Get().IsInSlotSetup(); }
    bool SpellSystemController::IsShoutActive() const { return IntegratedMagic::MagicState::Get().IsShoutActive(); }

}