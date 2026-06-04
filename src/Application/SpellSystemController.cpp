#include "Application/SpellSystemController.h"

#include "Adapters/Outbound/EquipSlots.h"
#include "Adapters/Outbound/MagicEquip.h"
#include "Adapters/Outbound/RestoreEquip.h"
#include "Adapters/Outbound/SyntheticInput.h"
#include "Application/InputController.h"
#include "Config/Slots.h"
#include "Domain/SlotCooldownTracker.h"
#include "Domain/State.h"
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

            if (autoResult.releaseLeftAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: release Left (UP only, DOWN next frame)");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, 0.1f);
            }
            if (autoResult.releaseRightAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: release Right (UP only, DOWN next frame)");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, 0.1f);
            }

            if (autoResult.startLeftAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: start Left (DOWN only, rising edge)");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
            }
            if (autoResult.startRightAttack) {
                MAGIC_DEBUG_LOG("[FLOW] OnFrame: start Right (DOWN only, rising edge)");
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

            if (!action.spellsToEquip.empty()) {
                const auto eqResult = state.OnEquipComplete();
                MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: OnEquipComplete → dispatchL={} dispatchR={}",
                                eqResult.dispatchLeft, eqResult.dispatchRight);
                if (eqResult.dispatchLeft) {
                    MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: → DispatchAttack Left 1.0 (equip)");
                    IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
                }
                if (eqResult.dispatchRight) {
                    MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: → DispatchAttack Right 1.0 (equip)");
                    IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);
                }
            }

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
        MAGIC_DEBUG_LOG("[SpellSystem] AnimEvent: {}", tag);
        using enum IntegratedMagic::Hand;
        auto& state = IntegratedMagic::MagicState::Get();

        if (tag == "EnableBumper"sv) {
            const auto r = state.NotifyAttackEnabled();
            if (r.dispatchLeft) {
                MAGIC_DEBUG_LOG("[SpellSystem] NotifyAttackEnabled: → release Left (UP only, DOWN next frame)");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, 0.1f);
            }
            if (r.dispatchRight) {
                MAGIC_DEBUG_LOG("[SpellSystem] NotifyAttackEnabled: → release Right (UP only, DOWN next frame)");
                IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, 0.1f);
            }
        }
        if (tag == "CastStop"sv || tag == "RitualSpellOut"sv) {
            HandleExitAllResult(state.OnCastStop());
        }
        if (tag == "shoutStop"sv) {
            HandleExitAllResult(state.OnShoutStop());
        }
        if (tag == "blockStart"sv || tag == "BashExit"sv) {
            if (!state.IsPressMode()) {
                HandleForceExitResult(state.ForceExit());
            }
        }

        if (tag == "staggerStart"sv || tag == "KnockDown"sv) {
            if (!state.IsPressMode()) {
                MAGIC_DEBUG_LOG("[SpellSystem] AnimEvent: {} → ForceExit", tag);
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
        InputController::Get().ResetInputState();
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
        auto& s = IntegratedMagic::MagicState::Get();
        if (!s.IsActive()) return {};
        return {s.ActiveLeftID(), s.ActiveRightID(), s.ActiveShoutID()};
    }

    void SpellSystemController::ExecuteRestoreSnapshotPlan(const IntegratedMagic::RestoreSnapshotPlan& plan) const {
        MAGIC_DEBUG_LOG(
            "[SpellSystem] ExecuteRestoreSnapshotPlan: valid={} restoreR={} restoreL={} clearVoice={} prevExtra={}",
            plan.valid, plan.restoreRightHand, plan.restoreLeftHand, plan.clearVoiceShout,
            plan.prevExtraEquipped.size());
        using enum IntegratedMagic::Hand;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!player || !mgr || !plan.valid) return;

        if (plan.applySkipEquipAnimReturn) {
            IntegratedMagic::MagicAction::ApplySkipEquipAnimReturn(player, plan.skipEquipAnimReturn);
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
                IntegratedMagic::MagicAction::EquipSpellInHand(player, plan.equipRightSpell, Right,
                                                               plan.skipEquipAnimReturn);
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
                IntegratedMagic::MagicAction::EquipSpellInHand(player, plan.equipLeftSpell, Left,
                                                               plan.skipEquipAnimReturn);
            }

            if (plan.restoreRightAfterLeftOnly) {
                IntegratedMagic::Outbound::RestoreOneHand(player, mgr, plan.inventoryIndex, false, plan.rightObj,
                                                          rightSlot);
            }
        }

        if (plan.clearVoiceShout) {
            // Major powers (kPower) have a 24h in-game cooldown. voiceRecoveryTime is 0 for powers
            // in Skyrim SE (only set for shouts), so we compute the duration from the timescale.
            {
                const auto& rd = player->GetActorRuntimeData();
                if (rd.selectedPower) {
                    if (auto* power = rd.selectedPower->As<RE::SpellItem>()) {
                        if (power->GetSpellType() == RE::MagicSystem::SpellType::kPower) {
                            const RE::FormID formID = power->GetFormID();
                            float timescale = 20.0f;
                            if (auto* cal = RE::Calendar::GetSingleton()) {
                                const float ts = cal->GetTimescale();
                                if (ts >= 1.0f) timescale = ts;
                            }
                            const float totalCooldown = 86400.0f / timescale;
                            const int n = static_cast<int>(IntegratedMagic::Slots::GetSlotCount());
                            for (int i = 0; i < n; ++i) {
                                if (IntegratedMagic::Slots::GetSlotShout(i) == formID) {
                                    IntegratedMagic::SlotCooldownTracker::Get().StartPowerCooldown(
                                        i, formID, totalCooldown);
                                    MAGIC_DEBUG_LOG(
                                        "[SpellSystem] StartPowerCooldown: slot={} formID={:#010x} "
                                        "totalCooldown={:.1f}s (timescale={:.1f})",
                                        i, formID, totalCooldown, timescale);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
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
        MAGIC_DEBUG_LOG(
            "[SpellSystem] HandleExitAllResult: L={} R={} shout={} restore={} finalize={} waitSheathe={} waitPower={}",
            result.leftAttack.has_value(), result.rightAttack.has_value(), result.shout.has_value(),
            result.restorePlan.has_value(), result.finalizeAfterController, result.waitForSheatheRestore,
            result.waitForPowerRestore);
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
        MAGIC_DEBUG_LOG("[SpellSystem] HandleForceExitResult: L={} R={} restore={} finalize={} resetShout={}",
                        result.leftAttack.has_value(), result.rightAttack.has_value(), result.restorePlan.has_value(),
                        result.finalizeAfterController, result.resetShoutAfterController);
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

    void SpellSystemController::NotifyUnexpectedUnequip(RE::TESBoundObject* base) const {
        IntegratedMagic::MagicState::Get().NotifyUnexpectedUnequip(base);
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

        const auto r = IntegratedMagic::MagicState::Get().OnCasterInterrupt(*hand, spell, depleteEnergy);
        if (r.releaseLeft) {
            MAGIC_DEBUG_LOG("[SpellSystem] OnCastInterrupted: → release Left (UP only, DOWN next frame)");
            IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, 0.1f);
        }
        if (r.releaseRight) {
            MAGIC_DEBUG_LOG("[SpellSystem] OnCastInterrupted: → release Right (UP only, DOWN next frame)");
            IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, 0.1f);
        }
        if (r.finishedLeft != -1.f)
            IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 0.0f, r.finishedLeft);
        if (r.finishedRight != -1.f)
            IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 0.0f, r.finishedRight);
    }

}