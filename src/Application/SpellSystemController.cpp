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
        static SpellSystemController inst;
        return inst;
    }

    void SpellSystemController::OnFrame(float dt, bool inputBlocked) const {
        DispatchSlotEvents();

        if (!inputBlocked) {
            const auto aaResult = IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);

            if (aaResult.leftAttack)
                IntegratedMagic::detail::DispatchAttack(aaResult.leftAttack->hand, aaResult.leftAttack->power,
                                                        aaResult.leftAttack->secsHeld);
            if (aaResult.rightAttack)
                IntegratedMagic::detail::DispatchAttack(aaResult.rightAttack->hand, aaResult.rightAttack->power,
                                                        aaResult.rightAttack->secsHeld);

            if (aaResult.shout) IntegratedMagic::detail::DispatchShout(aaResult.shout->power, aaResult.shout->secsHeld);
            const auto autoResult = IntegratedMagic::MagicState::Get().PumpAutomatic(dt);

            if (autoResult.startLeftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);

            if (autoResult.startRightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);

            if (autoResult.stopLeftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f,
                                                        autoResult.stopLeftAttack->heldSecs);

            if (autoResult.stopRightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f,
                                                        autoResult.stopRightAttack->heldSecs);

            if (autoResult.stopShout) IntegratedMagic::detail::DispatchShout(0.0f, autoResult.stopShout->heldSecs);

            if (autoResult.restorePlan) ExecuteRestoreSnapshotPlan(*autoResult.restorePlan);

            if (autoResult.finalizeAfterExecution)
                IntegratedMagic::MagicState::Get().FinalizeRestoreSnapshotPlan(autoResult.resetShoutAfterExecution);
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

            if (action.leftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, action.leftAttack->heldSecs);

            if (action.rightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f,
                                                        action.rightAttack->heldSecs);

            if (action.shout) IntegratedMagic::detail::DispatchShout(0.0f, action.shout->heldSecs);

            if (action.restorePlan) ExecuteRestoreSnapshotPlan(*action.restorePlan);

            if (action.finalizeAfterController) state.FinalizeRestoreSnapshotPlan(action.resetShoutAfterController);

            for (auto& intent : action.spellsToEquip)
                IntegratedMagic::MagicAction::EquipSpellInHand(player, intent.spell, intent.hand, action.skipAnim);

            if (action.shoutToEquip) IntegratedMagic::MagicAction::EquipShoutInVoice(player, action.shoutToEquip);

            if (action.startShoutDispatch) IntegratedMagic::detail::DispatchShout(1.0f, 0.0f);

            if (!action.spellsToEquip.empty()) state.OnEquipComplete(action.inventorySnapshotBefore);
        }

        for (auto s = input.ConsumeReleasedSlot(); s.has_value(); s = input.ConsumeReleasedSlot()) {
            MAGIC_DEBUG_LOG("[SpellSystem] HandleSlotReleased: slot={}", *s);
            HandleExitAllResult(state.OnSlotReleased(*s));
        }
    }

    void SpellSystemController::NotifyAnimEvent(std::string_view tag) const {
        using enum IntegratedMagic::Hand;
        auto& state = IntegratedMagic::MagicState::Get();

        if (tag == "EnableBumper"sv) {
            const auto r = state.NotifyAttackEnabled();
            if (auto* p = RE::PlayerCharacter::GetSingleton()) IntegratedMagic::MagicAction::DisableSkipEquipVarsNow(p);
            if (r.dispatchLeft) IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
            if (r.dispatchRight) IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);
        }
        if (tag == "CastStop"sv || tag == "RitualSpellOut"sv) {
            HandleExitAllResult(state.OnCastStop());
        }
        if (tag == "InterruptCast"sv) {
            const auto r = state.OnCastInterrupt();
            if (r.finishedLeft != -1.f)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, r.finishedLeft);
            if (r.finishedRight != -1.f)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, r.finishedRight);
        }
        if (tag == "BeginCastRight"sv) state.OnBeginCast(Right);
        if (tag == "BeginCastLeft"sv) state.OnBeginCast(Left);
        if (tag == "shoutStop"sv) {
            HandleExitAllResult(state.OnShoutStop());
        }
        if (tag == "blockStart"sv || tag == "BashExit"sv) {
            if (!state.IsPressMode()) {
                HandleForceExitResult(state.ForceExit());
            }
        }
        if (tag == "tailMTIdle"sv || tag == "IdleStop"sv) {
            if (state.IsWaitingSheatheRestore()) state.NotifySheatheComplete();
        }
        if (tag == "MRh_SpellFire_Event"sv) {
            const auto r = state.OnSpellFired(Right);

            if (r.leftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, r.leftAttack->heldSecs);
            if (r.rightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, r.rightAttack->heldSecs);

            if (r.finalizeLeft) state.ScheduleSpellFireFinalize(Left);
            if (r.finalizeRight) state.ScheduleSpellFireFinalize(Right);
        }

        if (tag == "MLh_SpellFire_Event"sv) {
            const auto r = state.OnSpellFired(Left);

            if (r.leftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, r.leftAttack->heldSecs);
            if (r.rightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, r.rightAttack->heldSecs);

            if (r.finalizeLeft) state.ScheduleSpellFireFinalize(Left);
            if (r.finalizeRight) state.ScheduleSpellFireFinalize(Right);
        }
    }

    void SpellSystemController::NotifyPlayerDeath() const {
        auto& state = IntegratedMagic::MagicState::Get();
        HandleForceExitResult(state.ForceExit());
    }

    void SpellSystemController::NotifyLoadGame() const {
        auto& state = IntegratedMagic::MagicState::Get();
        HandleForceExitResult(state.ForceExit());
    }

    void SpellSystemController::NotifyMenuOpen(std::string_view menuName) const {
        static constexpr std::array kInterruptMenus = {
            "ContainerMenu"sv, "InventoryMenu"sv, "MagicMenu"sv, "MapMenu"sv, "Journal Menu"sv, "Dialogue Menu"sv,
        };
        for (auto m : kInterruptMenus) {
            if (menuName == m) {
                auto& state = IntegratedMagic::MagicState::Get();
                HandleForceExitResult(state.ForceExit());
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

    void SpellSystemController::OnConfigChanged() const { InputController::Get().OnConfigChanged(); }
    void SpellSystemController::NotifyForeignEquip() const {
        auto& state = IntegratedMagic::MagicState::Get();
        HandleForceExitResult(state.ForceExitNoRestore());
    }
    bool SpellSystemController::IsSpellSystemActive() const { return IntegratedMagic::MagicState::Get().IsActive(); }
    int SpellSystemController::ActiveSlot() const { return IntegratedMagic::MagicState::Get().ActiveSlot(); }
    bool SpellSystemController::IsInSlotSetup() const { return IntegratedMagic::MagicState::Get().IsInSlotSetup(); }
    bool SpellSystemController::IsShoutActive() const { return IntegratedMagic::MagicState::Get().IsShoutActive(); }

    void SpellSystemController::ExecuteRestoreSnapshotPlan(const IntegratedMagic::RestoreSnapshotPlan& plan) const {
        using enum IntegratedMagic::Hand;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!player || !mgr || !plan.valid) return;

        if (plan.applySkipEquipAnimReturn) {
            const bool skip = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
            IntegratedMagic::MagicAction::ApplySkipEquipAnimReturn(player, skip);
        }

        const auto* rightSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(Right);
        const auto* leftSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(Left);

        if (plan.restoreRightHand) {
            if (plan.clearRightHandByRef) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, plan.clearRightHandByRef, Right);
            } else if (plan.clearRightHand) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, Right);
            }

            IntegratedMagic::Outbound::RestoreOneHand(player, mgr, plan.inventoryIndex, false, plan.rightObj,
                                                      rightSlot);

            if (plan.equipRightSpell) {
                const bool skip = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
                IntegratedMagic::MagicAction::EquipSpellInHand(player, plan.equipRightSpell, Right, skip);
            }
        }

        if (plan.restoreLeftHand) {
            if (plan.clearLeftHandByRef) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, plan.clearLeftHandByRef, Left);
            } else if (plan.clearLeftHand) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, Left);
            }

            IntegratedMagic::Outbound::RestoreOneHand(player, mgr, plan.inventoryIndex, true, plan.leftObj, leftSlot);

            if (plan.equipLeftSpell) {
                const bool skip = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
                IntegratedMagic::MagicAction::EquipSpellInHand(player, plan.equipLeftSpell, Left, skip);
            }

            if (plan.restoreRightAfterLeftOnly) {
                IntegratedMagic::Outbound::RestoreOneHand(player, mgr, plan.inventoryIndex, false, plan.rightObj,
                                                          rightSlot);
            }
        }

        if (plan.clearVoiceShout) {
            IntegratedMagic::MagicAction::ClearVoiceShout(player);
            if (plan.equipVoiceForm) {
                IntegratedMagic::MagicAction::EquipShoutInVoice(player, plan.equipVoiceForm);
            }
        }

        if (!plan.prevExtraEquipped.empty()) {
            IntegratedMagic::Outbound::ReequipPrevExtraEquipped(
                player, mgr, plan.inventoryIndex,
                const_cast<std::vector<IntegratedMagic::ExtraEquippedItem>&>(plan.prevExtraEquipped));
        }
    }

    void SpellSystemController::HandleExitAllResult(IntegratedMagic::ExitAllResult result) const {
        using enum IntegratedMagic::Hand;
        auto& state = IntegratedMagic::MagicState::Get();

        if (result.leftAttack) {
            IntegratedMagic::detail::DispatchAttack(Left, 0.0f, result.leftAttack->heldSecs);
        }

        if (result.rightAttack) {
            IntegratedMagic::detail::DispatchAttack(Right, 0.0f, result.rightAttack->heldSecs);
        }

        if (result.shout) {
            IntegratedMagic::detail::DispatchShout(0.0f, result.shout->heldSecs);
        }

        if (result.restorePlan) {
            ExecuteRestoreSnapshotPlan(*result.restorePlan);
        }

        if (result.finalizeExitAfterController) {
            state.FinalizeRestoreSnapshotPlan();
        }
    }

    void SpellSystemController::HandleForceExitResult(IntegratedMagic::ForceExitResult result) const {
        using enum IntegratedMagic::Hand;
        auto& state = IntegratedMagic::MagicState::Get();

        if (result.leftAttack) {
            IntegratedMagic::detail::DispatchAttack(Left, 0.0f, result.leftAttack->heldSecs);
        }

        if (result.rightAttack) {
            IntegratedMagic::detail::DispatchAttack(Right, 0.0f, result.rightAttack->heldSecs);
        }

        if (result.restorePlan) {
            ExecuteRestoreSnapshotPlan(*result.restorePlan);
        }

        if (result.finalizeAfterController) {
            state.FinalizeRestoreSnapshotPlan();
            if (result.resetShoutAfterController) {
                state.ResetShoutState();
            }
        }
    }

    void SpellSystemController::ConsumeForceExitResult(IntegratedMagic::ForceExitResult result) const {
        HandleForceExitResult(std::move(result));
    }

}