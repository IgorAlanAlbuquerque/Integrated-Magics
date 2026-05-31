#include "Application/HudController.h"

#include "Adapters/Inbound/HoveredForm.h"
#include "Application/AssignService.h"
#include "Application/InputController.h"
#include "Config/ConfigAdapter.h"
#include "Domain/SlotCooldownTracker.h"
#include "Domain/SlotCostUtil.h"
#include "Shared/SpellClassify.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Shared/Hand.h"
#include "Shared/HudIntents.h"
#include "Shared/InputIntents.h"
#include "UI/HudManager.h"
#include "UI/HudState.h"
#include "UI/HudView.h"

namespace Application {

    HudController& HudController::Get() {
        static HudController inst;
        return inst;
    }

    namespace {
        bool ComputeHardBlocked() {
            if (!RE::PlayerCharacter::GetSingleton()) return true;
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString mainMenu{"Main Menu"};
            static const RE::BSFixedString loadingMenu{"Loading Menu"};
            static const RE::BSFixedString faderMenu{"Fader Menu"};
            return ui->IsMenuOpen(mainMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(faderMenu);
        }

        bool ComputeSoftBlocked() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString menus[] = {
                "MagicMenu"sv,
                "TweenMenu"sv,
                "InventoryMenu"sv,
                "StatsMenu"sv,
                "MapMenu"sv,
                "Journal Menu"sv,
                "ContainerMenu"sv,
                "BarterMenu"sv,
                "Crafting Menu"sv,
                "Lockpicking Menu"sv,
                "Sleep/Wait Menu"sv,
                "Dialogue Menu"sv,
                "Console"sv,
                "Mod Configuration Menu"sv,
                "BestiaryMenu"sv,
                "OstimSceneMenu"sv,
                "Dialogue Topic Menu"sv,
            };
            for (const auto& m : menus)
                if (ui->IsMenuOpen(m)) return true;
            return false;
        }

        bool ComputeInMagicMenu() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return false;
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            return ui->IsMenuOpen(magicMenu);
        }

        bool EvaluateHudVisibility() {
            const auto& hud = IntegratedMagic::Config::MagicConfigAdapter::Get();
            using enum IntegratedMagic::Config::HudVisibilityFlag;

            if (hud.FlagSet(Always)) return true;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;

            if (hud.FlagSet(SlotActive) && IntegratedMagic::MagicState::Get().IsActive()) return true;
            if (hud.FlagSet(InCombat) && player->IsInCombat()) return true;
            if (hud.FlagSet(WeaponDrawn)) {
                using enum RE::WEAPON_STATE;
                const auto ws = player->AsActorState()->GetWeaponState();
                if (ws == kDrawn || ws == kWantToDraw || ws == kDrawing) return true;
            }
            return false;
        }

        void SetMagicMenuVisible(bool visible) {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return;
            auto menu = ui->GetMenu<RE::MagicMenu>();
            if (!menu || !menu->uiMovie) return;
            RE::GFxValue val(visible);
            menu->uiMovie->SetVariable("_root.Menu_mc._visible", val);
        }

        void HandleHudToggle() {
            using namespace IntegratedMagic::HUD;
            const bool willOpen = !g_popupOpen.load();
            g_popupOpen.store(willOpen);
            if (willOpen) {
                g_popupJustOpened.store(true, std::memory_order_relaxed);
                SetMagicMenuVisible(false);
            } else {
                SetMagicMenuVisible(true);
            }
        }
    }

    void HudController::OnFrame() {
        using namespace IntegratedMagic::HUD;
        auto& input = InputController::Get();

        const bool hardBlocked = ComputeHardBlocked();
        g_hardBlocked.store(hardBlocked);

        if (hardBlocked) {
            if (g_popupOpen.load()) g_popupOpen.store(false);

            Input::detail::g_popupOpenForInput.store(false, std::memory_order_relaxed);
            return;
        }

        const auto slotCount = static_cast<int>(IntegratedMagic::Slots::GetSlotCount());
        g_slotCount.store(slotCount);
        if (slotCount == 0) {
            Input::detail::g_popupOpenForInput.store(false, std::memory_order_relaxed);
            return;
        }

        const bool inMagicMenu = ComputeInMagicMenu();
        g_inMagicMenu.store(inMagicMenu);

        if (inMagicMenu && input.ConsumeHudToggle()) {
            HandleHudToggle();
        }
        if (!inMagicMenu && g_popupOpen.load()) {
            g_popupOpen.store(false);
        }

        g_softBlocked.store(ComputeSoftBlocked());
        g_hudShouldDraw.store(EvaluateHudVisibility());
        g_modifierHeld.store(input.IsModifierHeld());

        for (const auto& e : Input::detail::DrainPopupInputs()) {
            using K = Input::detail::PopupInputKind;
            switch (e.kind) {
                case K::MouseDelta:
                case K::StickDelta:
                    IntegratedMagic::HUD::FeedMouseDelta(e.dx, e.dy);
                    break;
                case K::Click:
                    IntegratedMagic::HUD::FeedMouseClick();
                    break;
                case K::RightClick:
                    IntegratedMagic::HUD::FeedMouseRightClick();
                    break;
                case K::Close:
                    g_popupOpen.store(false);
                    break;
            }
        }

        for (const auto& it : IntegratedMagic::HUD::DrainIntents()) {
            using IntegratedMagic::HUD::SlotIntentKind;
            switch (it.kind) {
                case SlotIntentKind::AssignHovered: {
                    const auto t = IntegratedMagic::HoveredForm::GetHoveredMagicType();
                    using HM = IntegratedMagic::HoveredForm::MagicType;
                    if (t == HM::Shout || t == HM::Power) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredShoutToSlot(it.slot);
                    } else if (t == HM::TwoHandedSpell) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(it.slot, IntegratedMagic::Hand::Left);
                    } else if (t == HM::RightOnlySpell) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(it.slot,
                                                                                  IntegratedMagic::Hand::Right);
                    } else if (t == HM::LeftOnlySpell) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(it.slot, IntegratedMagic::Hand::Left);
                    } else {
                        const auto hand = it.hoverRight ? IntegratedMagic::Hand::Right : IntegratedMagic::Hand::Left;
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(it.slot, hand);
                    }
                    break;
                }
                case SlotIntentKind::ClearSlot: {
                    const auto shID = IntegratedMagic::Slots::GetSlotShout(it.slot);
                    const auto rID = IntegratedMagic::Slots::GetSlotSpell(it.slot, IntegratedMagic::Hand::Right);
                    const auto lID = IntegratedMagic::Slots::GetSlotSpell(it.slot, IntegratedMagic::Hand::Left);
                    const bool slotIs2H =
                        !shID && !rID && lID &&
                        IntegratedMagic::SpellClassify::IsTwoHandedSpell(RE::TESForm::LookupByID<RE::SpellItem>(lID));

                    if (shID) {
                        IntegratedMagic::MagicAssign::TryClearSlotShout(it.slot);
                    } else if (slotIs2H) {
                        IntegratedMagic::MagicAssign::TryClearSlotHand(it.slot, IntegratedMagic::Hand::Right);
                        IntegratedMagic::MagicAssign::TryClearSlotHand(it.slot, IntegratedMagic::Hand::Left);
                    } else {
                        const auto hand = it.hoverRight ? IntegratedMagic::Hand::Right : IntegratedMagic::Hand::Left;
                        IntegratedMagic::MagicAssign::TryClearSlotHand(it.slot, hand);
                    }
                    break;
                }
                case SlotIntentKind::ClosePopup:
                    IntegratedMagic::Config::MagicConfigAdapter::Get().FlushSpellSettingsIfDirty();
                    g_popupOpen.store(false);
                    break;
            }
        }

        static bool s_lastPopupOpen = false;
        const bool nowPopupOpen = g_popupOpen.load();
        if (s_lastPopupOpen && !nowPopupOpen) {
            SetMagicMenuVisible(true);
        }
        s_lastPopupOpen = nowPopupOpen;

        const float dt = input.GetDeltaTime();
        IntegratedMagic::SlotCooldownTracker::Get().Update(dt);

        auto const& cfg = IntegratedMagic::Config::MagicConfigAdapter::Get();

        IntegratedMagic::HUD::HudView v{};
        v.slotCount = static_cast<int>(IntegratedMagic::Slots::GetSlotCount());
        v.activeSlot = IntegratedMagic::MagicState::Get().ActiveSlot();
        v.spellSystemActive = IntegratedMagic::MagicState::Get().IsActive();
        v.modifierHeld = input.IsModifierHeld();
        v.modifierKbPos = cfg.ModifierKbPosition();
        v.modifierGpPos = cfg.ModifierGpPosition();

        if (v.slotCount > 0) {
            const auto bind0 = cfg.GetSlotBinding(0);
            if (v.modifierKbPos > 0) v.modifierKbCode = bind0.kb[v.modifierKbPos - 1];
            if (v.modifierGpPos > 0) v.modifierGpCode = bind0.gp[v.modifierGpPos - 1];
        }

        const int n = std::min(v.slotCount, IntegratedMagic::HUD::kMaxViewSlots);
        for (int i = 0; i < n; ++i) {
            auto& s = v.slots[i];

            const auto rID = IntegratedMagic::Slots::GetSlotSpell(i, IntegratedMagic::Hand::Right);
            const auto lID = IntegratedMagic::Slots::GetSlotSpell(i, IntegratedMagic::Hand::Left);
            const auto shID = IntegratedMagic::Slots::GetSlotShout(i);

            s.rightSpellID = rID;
            s.leftSpellID = lID;
            s.shoutFormID = shID;
            s.rightSpell = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
            s.leftSpell = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
            s.isTwoHanded =
                !shID && !rID && s.leftSpell && IntegratedMagic::SpellClassify::IsTwoHandedSpell(s.leftSpell);

            if (shID) {
                s.labelForm = RE::TESForm::LookupByID(shID);
            } else if (s.isTwoHanded) {
                s.labelForm = s.leftSpell;
            }

            const auto afford = IntegratedMagic::ComputeSlotAffordability(i);
            s.hasSpells = afford.hasSpells;
            s.canCast = afford.canCast;

            const auto cd = IntegratedMagic::SlotCooldownTracker::Get().GetSlotInfo(i);
            s.onCooldown = cd.onCooldown;
            s.justFinishedCooldown = cd.justFinished;
            s.cooldownProgress = cd.progress;

            const auto b = cfg.GetSlotBinding(i);
            s.kbCodes = {b.kb[0], b.kb[1], b.kb[2]};
            s.gpCodes = {b.gp[0], b.gp[1], b.gp[2]};
        }

        IntegratedMagic::HUD::StoreHudView(v);

        if (inMagicMenu && !nowPopupOpen) {
            static std::uint64_t s_wasDown = 0;
            std::uint64_t nowDown = 0;
            for (int i = 0; i < n; ++i) {
                if (input.IsSlotHotkeyDown(i)) nowDown |= (1uLL << static_cast<std::uint64_t>(i));
            }
            const std::uint64_t justPressed = nowDown & ~s_wasDown;
            s_wasDown = nowDown;

            if (justPressed) {
                const auto t = IntegratedMagic::HoveredForm::GetHoveredMagicType();
                using HM = IntegratedMagic::HoveredForm::MagicType;
                for (int i = 0; i < n; ++i) {
                    if (!(justPressed & (1uLL << static_cast<std::uint64_t>(i)))) continue;
                    if (t == HM::Shout || t == HM::Power) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredShoutToSlot(i);
                    } else if (t == HM::TwoHandedSpell || t == HM::LeftOnlySpell) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(i, IntegratedMagic::Hand::Left);
                    } else if (t == HM::RightOnlySpell) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(i, IntegratedMagic::Hand::Right);
                    } else if (t != HM::None) {
                        IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(i, IntegratedMagic::Hand::Left);
                    }
                }
            }
        }

        Input::detail::g_popupOpenForInput.store(nowPopupOpen, std::memory_order_relaxed);
    }
}