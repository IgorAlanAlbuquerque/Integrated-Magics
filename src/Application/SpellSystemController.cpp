#include "Application/SpellSystemController.h"

#include "Shared/HoveredFormState.h"
#include "Adapters/Outbound/EquipSlots.h"
#include "Adapters/Outbound/MagicEquip.h"
#include "Adapters/Outbound/RestoreEquip.h"
#include "Adapters/Outbound/SyntheticInput.h"
#include "Application/AssignService.h"
#include "Application/InputController.h"
#include "Config/ConfigAdapter.h"
#include "Config/Slots.h"
#include "Domain/State.h"
#include "Input/HotkeyMatcher.h"
#include "PCH.h"

namespace Application {

    namespace {
        std::optional<IntegratedMagic::Hand> SourceToHand(RE::MagicSystem::CastingSource src) {
            using enum RE::MagicSystem::CastingSource;
            switch (src) {
                case kLeftHand:
                    return IntegratedMagic::Hand::Left;
                case kRightHand:
                    return IntegratedMagic::Hand::Right;
                default:
                    return std::nullopt;
            }
        }
    }

    SpellSystemController& SpellSystemController::Get() {
        static SpellSystemController inst;
        return inst;
    }

    void SpellSystemController::OnFrame(float dt, bool inputBlocked) const {
        DispatchSlotEvents();

        if (!inputBlocked) {
            const auto aaResult = IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);

            if (aaResult.leftAttack) {
#ifdef DEBUG
                static float s_lastLeftDispatchLog = -2.f;
                const float leftHeld = aaResult.leftAttack->secsHeld;
                if ((leftHeld < s_lastLeftDispatchLog) || (leftHeld - s_lastLeftDispatchLog >= 1.0f)) {
                    s_lastLeftDispatchLog = leftHeld;
                    MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching aa.leftAttack power={:.2f} held={:.3f}",
                                    aaResult.leftAttack->power, leftHeld);
                }
#endif
                IntegratedMagic::detail::DispatchAttack(aaResult.leftAttack->hand, aaResult.leftAttack->power,
                                                        aaResult.leftAttack->secsHeld);
            }
            if (aaResult.rightAttack) {
#ifdef DEBUG
                static float s_lastRightDispatchLog = -2.f;
                const float rightHeld = aaResult.rightAttack->secsHeld;
                if ((rightHeld < s_lastRightDispatchLog) || (rightHeld - s_lastRightDispatchLog >= 1.0f)) {
                    s_lastRightDispatchLog = rightHeld;
                    MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching aa.rightAttack power={:.2f} held={:.3f}",
                                    aaResult.rightAttack->power, rightHeld);
                }
#endif
                IntegratedMagic::detail::DispatchAttack(aaResult.rightAttack->hand, aaResult.rightAttack->power,
                                                        aaResult.rightAttack->secsHeld);
            }

            if (aaResult.shout) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching aa.shout power={:.2f} held={:.3f}", aaResult.shout->power,
                                aaResult.shout->secsHeld);
                IntegratedMagic::detail::DispatchShout(aaResult.shout->power, aaResult.shout->secsHeld);
            }

            const auto autoResult = IntegratedMagic::MagicState::Get().PumpAutomatic(dt);

            if (autoResult.startLeftAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching startLeftAttack value=1.0 held=0.0");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
            }

            if (autoResult.startRightAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching startRightAttack value=1.0 held=0.0");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);
            }

            if (autoResult.stopLeftAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching stopLeftAttack value=0.0 held={:.3f}",
                                autoResult.stopLeftAttack->heldSecs);
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f,
                                                        autoResult.stopLeftAttack->heldSecs);
            }

            if (autoResult.stopRightAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching stopRightAttack value=0.0 held={:.3f}",
                                autoResult.stopRightAttack->heldSecs);
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f,
                                                        autoResult.stopRightAttack->heldSecs);
            }

            if (autoResult.stopShout) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: dispatching stopShout value=0.0 held={:.3f}",
                                autoResult.stopShout->heldSecs);
                IntegratedMagic::detail::DispatchShout(0.0f, autoResult.stopShout->heldSecs);
            }

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

            if (action.restorePlan) ExecuteRestoreSnapshotPlan(*action.restorePlan);

            if (action.finalizeAfterController) state.FinalizeRestoreSnapshotPlan(action.resetShoutAfterController);

            if (action.needsSkipEquipVars) {
                IntegratedMagic::MagicAction::ResetSkipEquipToken();
                IntegratedMagic::MagicAction::SetSkipEquipVars(player, true);
                player->DrawWeaponMagicHands(true);
            }

            for (auto& intent : action.spellsToEquip) {
                IntegratedMagic::MagicAction::EquipSpellInHand(player, intent.spell, intent.hand, action.skipAnim);
            }

            if (action.shoutToEquip) IntegratedMagic::MagicAction::EquipShoutInVoice(player, action.shoutToEquip);

            if (action.startShoutDispatch) IntegratedMagic::detail::DispatchShout(1.0f, 0.0f);

            if (!action.spellsToEquip.empty()) state.OnEquipComplete(action.inventorySnapshotBefore);

            if (action.leftAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, action.leftAttack->heldSecs);

            if (action.rightAttack)
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f,
                                                        action.rightAttack->heldSecs);

            if (action.shout) IntegratedMagic::detail::DispatchShout(0.0f, action.shout->heldSecs);
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

    SpellSystemController::ActiveSlotContents SpellSystemController::GetActiveSlotContents() const {
        const int slot = ActiveSlot();
        if (slot < 0) return {};
        return {
            IntegratedMagic::Slots::GetSlotSpell(slot, IntegratedMagic::Hand::Left),
            IntegratedMagic::Slots::GetSlotSpell(slot, IntegratedMagic::Hand::Right),
            IntegratedMagic::Slots::GetSlotShout(slot),
        };
    }

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

    void SpellSystemController::HandleExitAllResult(IntegratedMagic::StateExitResult result) const {
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

        if (result.finalizeAfterController) {
            state.FinalizeRestoreSnapshotPlan();
        }
    }

    void SpellSystemController::HandleForceExitResult(IntegratedMagic::StateExitResult result) const {
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

    void SpellSystemController::ConsumeForceExitResult(IntegratedMagic::StateExitResult result) const {
        HandleForceExitResult(std::move(result));
    }

    void SpellSystemController::OnCastStarted(RE::MagicSystem::CastingSource src, RE::MagicItem* spell,
                                              RE::MagicSystem::CastingType type) const {
        const auto hand = SourceToHand(src);
        if (!hand) return;

        MAGIC_DEBUG_LOG("[SpellSystem] OnCastStarted: hand={} spell={:#010x} type={}",
                        *hand == IntegratedMagic::Hand::Left ? "Left" : "Right", spell ? spell->GetFormID() : 0u,
                        static_cast<int>(type));

        IntegratedMagic::MagicState::Get().OnCasterStartCast(*hand, spell, type);
    }

    void SpellSystemController::OnCastInterrupted(RE::MagicSystem::CastingSource src, RE::MagicItem* spell,
                                                  bool depleteEnergy) const {
        const auto hand = SourceToHand(src);
        if (!hand) return;

        MAGIC_DEBUG_LOG("[SpellSystem] OnCastInterrupted: hand={} spell={:#010x} depleteEnergy={}",
                        *hand == IntegratedMagic::Hand::Left ? "Left" : "Right", spell ? spell->GetFormID() : 0u,
                        depleteEnergy);

        IntegratedMagic::MagicState::Get().OnCasterInterrupt(*hand, spell, depleteEnergy);
    }

}