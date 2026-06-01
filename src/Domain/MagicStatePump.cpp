#include <utility>

#include "Config/ConfigAdapter.h"
#include "Config/Slots.h"
#include "Domain/CasterUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/InventoryUtil.h"

namespace IntegratedMagic {
    PumpResult MagicState::PumpAutoAttack(float dt) {
        using enum Hand;
        const float add = dt > 0.f ? dt : 0.f;
        PumpResult result;

        if (_aa.heldLeft) {
            _aa.secsLeft += add;
            result.leftAttack = {Left, 1.0f, _aa.secsLeft};
            static int leftTick = 0;
            if ((++leftTick % 30) == 0)
                MAGIC_DEBUG_LOG("[FLOW] PumpAutoAttack: continuous press Left heldSecs={:.2f}", _aa.secsLeft);
        }
        if (_aa.heldRight) {
            _aa.secsRight += add;
            result.rightAttack = {Right, 1.0f, _aa.secsRight};
            static int rightTick = 0;
            if ((++rightTick % 30) == 0)
                MAGIC_DEBUG_LOG("[FLOW] PumpAutoAttack: continuous press Right heldSecs={:.2f}", _aa.secsRight);
        }
        if (_shout.held) {
            _shout.heldSecs += add;
            result.shout = {1.0f, _shout.heldSecs};
        }

        return result;
    }

    void MagicState::ConfirmAutoCastStarted(Hand hand) {
        auto& hm = ModeFor(hand);

        if (!_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={} ignored - aaHeld=false",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        hm.autoCastPhase = AutoCastPhase::Casting;
        hm.waitingChargeComplete = true;
        hm.castingElapsedSecs = 0.f;

        MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={}", IsLeft(hand) ? "Left" : "Right");
    }

    bool MagicState::RequestAutoAttackStart(Hand hand, bool /*clearWaitAfterEquip*/) {
        auto& hm = ModeFor(hand);
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) || hm.finished) {
            MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} REJECT - not eligible or finished", handStr);
            return false;
        }

        if (_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} REJECT - aaHeld already true", handStr);
            return false;
        }

        _aa.Held(hand) = true;
        _aa.Secs(hand) = 0.f;
        hm.autoCastPhase = AutoCastPhase::StartRequested;
        hm.startRequestSecs = 0.f;

        MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} ACCEPTED - aaHeld=true phase=StartRequested", handStr);
        return true;
    }

    AttackEnabledResult MagicState::NotifyAttackEnabled() {
        if (!_session.active) return {};

        AttackEnabledResult result;
        using enum Hand;

        MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: EnableBumper received left.phase={} right.phase={}",
                        static_cast<int>(_left.autoCastPhase), static_cast<int>(_right.autoCastPhase));

        // EnableBumper signals the behaviour machine is ready to accept magic input.
        // If a hand is still in StartRequested, re-dispatch the initial press with correct timing.
        auto tryRedispatch = [&](Hand hand, bool& dispatchFlag) {
            auto& hm = ModeFor(hand);
            if (hm.autoCastPhase != AutoCastPhase::StartRequested) return;
            _aa.Secs(hand) = 0.f;
            hm.startRequestSecs = 0.f;
            dispatchFlag = true;
            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} re-dispatch", IsLeft(hand) ? "Left" : "Right");
        };

        tryRedispatch(Left, result.dispatchLeft);
        tryRedispatch(Right, result.dispatchRight);
        return result;
    }

    StateExitResult MagicState::OnCastStop() {
        using enum Hand;

        StateExitResult result{};
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] OnCastStop: ignored - not active");
            return result;
        }

        MAGIC_DEBUG_LOG(
            "[State] OnCastStop: isDualCasting={} "
            "left.autoActive={} left.chargeComplete={} left.finished={} "
            "right.autoActive={} right.chargeComplete={} right.finished={} "
            "left.holdFired={} right.holdFired={}",
            _session.isDualCasting, _left.autoActive, _left.chargeComplete, _left.finished, _right.autoActive,
            _right.chargeComplete, _right.finished, _left.holdFiredAndWaitingCastStop,
            _right.holdFiredAndWaitingCastStop);

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        // pressAutocast: reset auto flags, keep press active (spell stays equipped)
        auto handlePressAutocast = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && hm.pressAutocast &&
                hm.autoCastPhase != AutoCastPhase::Done) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.pressAutocast = false;
                hm.autoCastPhase = AutoCastPhase::Done;
            }
        };

        // Automatic mode charge complete → finish hand
        auto handleAutoComplete = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && !hm.pressAutocast &&
                hm.autoCastPhase != AutoCastPhase::Done) {
                const float finished = FinishHand(h);
                if (finished != -1.f) {
                    if (IsLeft(h))
                        result.leftAttack = StopDispatchIntent{finished};
                    else
                        result.rightAttack = StopDispatchIntent{finished};
                }
            }
        };

        handlePressAutocast(Left);
        handlePressAutocast(Right);
        handleAutoComplete(Left);
        handleAutoComplete(Right);

        // Hold mode: hotkey was released while charge spell was at full charge
        if (_left.holdFiredAndWaitingCastStop && !_left.finished) {
            const float finishedL = FinishHand(Left);
            if (finishedL != -1.f) result.leftAttack = StopDispatchIntent{finishedL};
        }

        if (_right.holdFiredAndWaitingCastStop && !_right.finished) {
            const float finishedR = FinishHand(Right);
            if (finishedR != -1.f) result.rightAttack = StopDispatchIntent{finishedR};
        }

        merge(TryFinalizeExit());
        return result;
    }

    StateExitResult MagicState::OnShoutStop() {
        StateExitResult result{};

        if (!_session.active || _shout.modeShoutID == 0 || _shout.finished) return result;
        if (_shout.isPower) return result;

        MAGIC_DEBUG_LOG("[State] OnShoutStop: modeShoutID={:#010x} waitingStopEvent={}", _shout.modeShoutID,
                        _shout.waitingStopEvent);

        const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
        if (!ss) return result;

        const bool isHold = (ss->mode == ActivationMode::Hold);
        const bool isAuto = (ss->mode == ActivationMode::Automatic);

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        if (isAuto || _shout.waitingStopEvent || isHold) {
            if (isHold && !_shout.waitingStopEvent) {
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    result.shout = StopDispatchIntent{held};
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }
            }

            _shout.waitingStopEvent = false;
            _shout.finished = true;

            merge(TryFinalizeExit());
        }

        return result;
    }

    PumpCastPhaseResult MagicState::PumpCastPhase(Hand hand, float dt) {
        PumpCastPhaseResult result{};
        auto& hm = ModeFor(hand);

        if (!_session.active || hm.finished) return result;

        const bool isAutoOrHold = hm.autoActive || (hm.holdActive && hm.wantAutoAttack);
        if (!isAutoOrHold && !hm.holdFiredAndWaitingCastStop) return result;

        if (hm.autoCastPhase == AutoCastPhase::Idle || hm.autoCastPhase == AutoCastPhase::Done) return result;

        auto* player = GetPlayer();
        if (!player) return result;

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
        const auto* caster = GetMagicCaster(player, src);
        if (!caster) return result;

        const auto casterState = std::to_underlying(caster->state.get());

#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
        static float s_lastPumpLog[2] = {-2.f, -2.f};
        const int hi = IsLeft(hand) ? 0 : 1;
        const float secs = _aa.Secs(hand);
        const bool shouldLog = (secs < s_lastPumpLog[hi]) || (secs - s_lastPumpLog[hi] >= 1.0f);
        if (shouldLog) s_lastPumpLog[hi] = secs;
        if (shouldLog) {
            MAGIC_DEBUG_LOG(
                "[FLOW] PumpCastPhase: hand={} phase={} casterState={} aaHeld={} secs={:.2f} chargeComplete={}",
                handStr, static_cast<int>(hm.autoCastPhase), casterState, _aa.Held(hand), secs, hm.chargeComplete);
        }
#endif

        // Transition: StartRequested → Casting (caster picked up the spell and started)
        if (hm.autoCastPhase == AutoCastPhase::StartRequested && casterState >= 1) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested → Casting (state={})", handStr, casterState);
            ConfirmAutoCastStarted(hand);
        }

        // Accumulate time in StartRequested — used for periodic re-dispatch fallback when the
        // behaviour machine takes time to accept magic input (e.g. weapon-drawn + spell stance).
        constexpr float kRedispatchInterval = 0.5f;
        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            const float prev = hm.startRequestSecs;
            hm.startRequestSecs += dt > 0.f ? dt : 0.f;
            if (hm.startRequestSecs >= kRedispatchInterval &&
                static_cast<int>(hm.startRequestSecs / kRedispatchInterval) >
                    static_cast<int>(prev / kRedispatchInterval)) {
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested for {:.2f}s → re-dispatch",
                                IsLeft(hand) ? "L" : "R", hm.startRequestSecs);
                _aa.Secs(hand) = 0.f;
                result.startAttack = true;
            }
        }

        // Accumulate time since cast was confirmed — used by OnCasterInterrupt to detect spurious interrupts.
        if (hm.autoCastPhase == AutoCastPhase::Casting) {
            hm.castingElapsedSecs += dt > 0.f ? dt : 0.f;
        }

        // Charge detection — ONLY for auto modes. Hold mode keeps button held via PumpAutoAttack until
        // OnSlotReleased fires, which handles charge-complete/holdFiredAndWaitingCastStop directly.
        if (hm.autoActive && hm.autoCastPhase == AutoCastPhase::Casting && hm.waitingChargeComplete) {
            const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, hand) : 0;
            const auto* spell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;

            if (spell && IsChargeComplete(caster, spell)) {
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} charge complete → WaitingChargeRelease", handStr);
                hm.autoCastPhase = AutoCastPhase::WaitingChargeRelease;
                hm.waitingChargeComplete = false;
                hm.chargeComplete = true;

                if (_aa.Held(hand)) {
                    const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
                    result.stopAttack = StopDispatchIntent{held};
                    _aa.Held(hand) = false;
                    _aa.Secs(hand) = 0.f;
                }
            }
        }

        // Cast ended: caster returned to Idle
        const bool autoModeEnded =
            hm.autoActive &&
            (hm.autoCastPhase == AutoCastPhase::Casting || hm.autoCastPhase == AutoCastPhase::WaitingChargeRelease) &&
            casterState == 0;
        const bool holdModeEnded = hm.holdFiredAndWaitingCastStop && casterState == 0;

        if (autoModeEnded || holdModeEnded) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} cast ended (state=0 phase={} holdFired={})", handStr,
                            static_cast<int>(hm.autoCastPhase), hm.holdFiredAndWaitingCastStop);

            if (hm.pressAutocast) {
                // Press mode one-shot: reset auto flags, keep spell equipped via pressActive
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.pressAutocast = false;
                hm.autoCastPhase = AutoCastPhase::Done;
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} pressAutocast reset", handStr);
            } else {
                const float finished = FinishHand(hand);
                if (finished != -1.f) result.stopAttack = StopDispatchIntent{finished};
            }
        }

        return result;
    }

    StateExitResult MagicState::PumpSpellFireFinalize(float dt) {
        StateExitResult result{};

        if (!_session.active) {
            _left.waitingSpellFireFinalize = false;
            _left.spellFireFinalizeSecs = 0.f;
            _right.waitingSpellFireFinalize = false;
            _right.spellFireFinalizeSecs = 0.f;
            return result;
        }

        auto merge = [&](StateExitResult&& src) {
            if (!result.leftAttack) result.leftAttack = src.leftAttack;
            if (!result.rightAttack) result.rightAttack = src.rightAttack;
            if (!result.shout) result.shout = src.shout;

            if (!result.restorePlan && src.restorePlan) {
                result.restorePlan = std::move(src.restorePlan);
            }

            result.waitForSheatheRestore = result.waitForSheatheRestore || src.waitForSheatheRestore;
            result.waitForPendingRestore = result.waitForPendingRestore || src.waitForPendingRestore;
            result.waitForPowerRestore = result.waitForPowerRestore || src.waitForPowerRestore;
            result.finalizeAfterController = result.finalizeAfterController || src.finalizeAfterController;
            result.resetShoutAfterController = result.resetShoutAfterController || src.resetShoutAfterController;
        };

        constexpr float kSpellFireFinalizeDelay = 0.7f;

        auto pumpOne = [&](Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingSpellFireFinalize) return;

            hm.spellFireFinalizeSecs += dt > 0.f ? dt : 0.f;
            if (hm.spellFireFinalizeSecs < kSpellFireFinalizeDelay) return;

            hm.waitingSpellFireFinalize = false;
            hm.spellFireFinalizeSecs = 0.f;

            merge(TryFinalizeExit());
        };

        using enum Hand;
        pumpOne(Left);
        pumpOne(Right);

        return result;
    }

    SpellFiredResult MagicState::OnSpellFired(Hand hand) {
        SpellFiredResult result{};

        if (!_session.active) return result;

        auto& hm = ModeFor(hand);

        if (!(hm.autoActive && !hm.finished && hm.chargeComplete)) return result;

        if (hm.pressAutocast) {
            hm.autoActive = false;
            hm.chargeComplete = false;
            hm.waitingChargeComplete = false;
            hm.pressAutocast = false;
            return result;
        }

        if (_session.isDualCasting) {
            using enum IntegratedMagic::Hand;

            const float finishedL = FinishHand(Left);
            if (finishedL != -1.f) {
                result.leftAttack = SpellFiredResult::StopHandDispatchIntent{Left, finishedL};
            }

            const float finishedR = FinishHand(Right);
            if (finishedR != -1.f) {
                result.rightAttack = SpellFiredResult::StopHandDispatchIntent{Right, finishedR};
            }

            _session.isDualCasting = false;

            result.finalizeLeft = true;
            result.finalizeRight = true;
        } else {
            const float finished = FinishHand(hand);
            if (finished != -1.f) {
                if (IsLeft(hand))
                    result.leftAttack = SpellFiredResult::StopHandDispatchIntent{hand, finished};
                else
                    result.rightAttack = SpellFiredResult::StopHandDispatchIntent{hand, finished};
            }

            if (IsLeft(hand))
                result.finalizeLeft = true;
            else
                result.finalizeRight = true;
        }

        return result;
    }

    void MagicState::ScheduleSpellFireFinalize(Hand hand) {
        auto& hm = ModeFor(hand);
        hm.waitingSpellFireFinalize = true;
        hm.spellFireFinalizeSecs = 0.f;
    }

    PumpAutomaticResult MagicState::PumpAutomatic(float dt) {
        PumpAutomaticResult result{};

        auto mergeExit = [&](StateExitResult&& src) {
            if (src.leftAttack && !result.stopLeftAttack)
                result.stopLeftAttack = StopDispatchIntent{src.leftAttack->heldSecs};

            if (src.rightAttack && !result.stopRightAttack)
                result.stopRightAttack = StopDispatchIntent{src.rightAttack->heldSecs};

            if (src.shout && !result.stopShout) result.stopShout = StopDispatchIntent{src.shout->heldSecs};

            if (!result.restorePlan && src.restorePlan) result.restorePlan = std::move(src.restorePlan);

            result.finalizeAfterExecution = result.finalizeAfterExecution || src.finalizeAfterController;

            result.resetShoutAfterExecution = result.resetShoutAfterExecution || src.resetShoutAfterController;
        };

        auto mergeForceExit = [&](StateExitResult&& src) {
            if (src.leftAttack && !result.stopLeftAttack)
                result.stopLeftAttack = StopDispatchIntent{src.leftAttack->heldSecs};

            if (src.rightAttack && !result.stopRightAttack)
                result.stopRightAttack = StopDispatchIntent{src.rightAttack->heldSecs};

            if (src.shout && !result.stopShout) result.stopShout = StopDispatchIntent{src.shout->heldSecs};

            if (!result.restorePlan && src.restorePlan) result.restorePlan = std::move(src.restorePlan);

            result.finalizeAfterExecution = result.finalizeAfterExecution || src.finalizeAfterController;

            result.resetShoutAfterExecution = result.resetShoutAfterExecution || src.resetShoutAfterController;
        };

        auto mergePumpPhase = [&](Hand hand, const PumpCastPhaseResult& src) {
            if (src.startAttack) {
                if (IsLeft(hand))
                    result.startLeftAttack = true;
                else
                    result.startRightAttack = true;
            }
            if (src.stopAttack) {
                if (IsLeft(hand)) {
                    if (!result.stopLeftAttack) result.stopLeftAttack = StopDispatchIntent{src.stopAttack->heldSecs};
                } else {
                    if (!result.stopRightAttack) result.stopRightAttack = StopDispatchIntent{src.stopAttack->heldSecs};
                }
            }
        };

        if (_restore.pendingPowerRestore) {
            if (_restore.pendingPowerRestoreDelaySecs > 0.f) {
                _restore.pendingPowerRestoreDelaySecs -= dt > 0.f ? dt : 0.f;
                return result;
            }

            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingPowerRestore -> RestoreSnapshot");

            _restore.pendingPowerRestore = false;
            _restore.pendingPowerRestoreDelaySecs = 0.f;

            if (auto* player = GetPlayer()) {
                auto plan = BuildRestoreSnapshotPlan(player);
                if (plan.valid) {
                    result.restorePlan = std::move(plan);
                    result.finalizeAfterExecution = true;
                }
            }

            return result;
        }

        if (_restore.pendingRestoreAfterSheathe) {
            if (auto* player = GetPlayer()) {
                _restore.sheatheWaitSecs += dt > 0.f ? dt : 0.f;
                const bool timedOut = _restore.sheatheWaitSecs >= RestoreContext::kSheatheWaitTimeoutSec;
                const bool giveUp = player->IsInCombat() ||
                                    player->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kWantToDraw ||
                                    timedOut;

                if (_restore.sheatheAnimComplete || giveUp) {
                    _restore.sheatheWaitSecs = 0.f;

                    MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingRestoreAfterSheathe -> restore (giveUp={})", giveUp);

                    _restore.pendingRestoreAfterSheathe = false;
                    _restore.sheatheAnimComplete = false;

                    auto plan = BuildRestoreSnapshotPlan(player);
                    if (plan.valid) {
                        result.restorePlan = std::move(plan);
                        result.finalizeAfterExecution = true;
                    }

                    ResetSessionState();
                }
            }

            return result;
        }

        if (_restore.pendingRestore) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingRestore -> RestoreSnapshot + deactivate");

            _restore.pendingRestore = false;

            if (auto* player = GetPlayer()) {
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    result.stopShout = StopDispatchIntent{held};
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }

                auto plan = BuildRestoreSnapshotPlan(player);
                if (plan.valid) {
                    result.restorePlan = std::move(plan);
                    result.finalizeAfterExecution = true;
                }
            }

            ResetSessionState();
            return result;
        }

        using enum Hand;

        mergePumpPhase(Left, PumpCastPhase(Left, dt));
        mergePumpPhase(Right, PumpCastPhase(Right, dt));
        mergeExit(PumpSpellFireFinalize(dt));

        if (!_session.active) return result;

        {
            static float sDumpAccum = 0.f;
            sDumpAccum += dt > 0.f ? dt : 0.f;
            if (sDumpAccum >= 2.0f) {
                sDumpAccum = 0.f;
                if ((_left.autoActive && !_left.finished) || (_right.autoActive && !_right.finished)) {
                    MAGIC_DEBUG_LOG(
                        "[State] PumpAutomatic: HEARTBEAT slot={} "
                        "L(auto={} charge={} charged={} aaHeld={} secs={:.1f} phase={}) "
                        "R(auto={} charge={} charged={} aaHeld={} secs={:.1f} phase={})",
                        _session.activeSlot, _left.autoActive, _left.waitingChargeComplete, _left.chargeComplete,
                        _aa.heldLeft, _aa.secsLeft, static_cast<int>(_left.autoCastPhase), _right.autoActive,
                        _right.waitingChargeComplete, _right.chargeComplete, _aa.heldRight, _aa.secsRight,
                        static_cast<int>(_right.autoCastPhase));
                }
            }
        }

        if (ShouldForceInterrupt()) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: ShouldForceInterrupt -> ForceExit");
            mergeForceExit(ForceExit());
            return result;
        }

        _session.activeTimeoutSecs += dt > 0.f ? dt : 0.f;
        if (_session.activeTimeoutSecs > kMaxActiveTimeoutSecs) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: TIMEOUT -> ForceExit");
            mergeForceExit(ForceExit());
            return result;
        }

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.held && !_shout.finished) {
            const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
            if (ss && ss->mode == ActivationMode::Automatic) {
                constexpr float kPowerAutoDuration = 0.2f;
                _shout.powerAutoSecs += dt > 0.f ? dt : 0.f;

                MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto secs={:.3f}/{:.3f}", _shout.powerAutoSecs,
                                kPowerAutoDuration);

                if (_shout.powerAutoSecs >= kPowerAutoDuration) {
                    MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto duration elapsed -> StopShoutPress + finish");

                    if (_shout.held) {
                        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                        result.stopShout = StopDispatchIntent{held};
                        _shout.held = false;
                        _shout.heldSecs = 0.f;
                    }

                    _shout.finished = true;
                    mergeExit(TryFinalizeExit());
                }
            }
        }

        return result;
    }

    void MagicState::OnCasterStartCast(Hand hand, const RE::MagicItem* spell,
                                       RE::MagicSystem::CastingType
#ifdef DEBUG
                                           type
#endif
    ) {
        if (!_session.active) return;

        auto& hm = ModeFor(hand);
        const auto* expected = IsLeft(hand) ? _session.modeSpellLeft : _session.modeSpellRight;
        if (!expected || spell != expected) return;

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return;
        if (hm.finished) return;

        MAGIC_DEBUG_LOG("[State] OnCasterStartCast: hand={} phase={} type={} spell={:#010x}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), static_cast<int>(type),
                        spell ? spell->GetFormID() : 0u);

        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            ConfirmAutoCastStarted(hand);
        }
    }

    CastInterruptResult MagicState::OnCasterInterrupt(Hand hand, const RE::MagicItem* spell,
                                                      bool
#ifdef DEBUG
                                                          depleteEnergy
#endif
    ) {
        CastInterruptResult result;
        if (!_session.active) return result;

        auto& hm = ModeFor(hand);
        const auto* expected = IsLeft(hand) ? _session.modeSpellLeft : _session.modeSpellRight;
        if (!expected) return result;
        if (spell && spell != expected) return result;

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return result;
        if (hm.finished) return result;

        // Only relevant while waiting for cast to start or actively casting
        if (hm.autoCastPhase != AutoCastPhase::StartRequested &&
            hm.autoCastPhase != AutoCastPhase::Casting) return result;

        MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} phase={} depleteEnergy={} castingSecs={:.3f}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), depleteEnergy,
                        hm.castingElapsedSecs);

        // Interrupt while StartRequested: cast never confirmed — always spurious (stance-transition
        // artifact that arrives before the frame pump can detect casterState >= 1). Re-dispatch.
        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            _aa.Secs(hand) = 0.f;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} pre-cast spurious → re-dispatch",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand)) result.restartLeft = true;
            else result.restartRight = true;
            return result;
        }

        // Interrupt while Casting: spurious if within 0.2s of cast confirmation, real otherwise.
        constexpr float kSpuriousInterruptWindow = 0.2f;
        const bool isSpurious = (hm.castingElapsedSecs < kSpuriousInterruptWindow);

        if (isSpurious) {
            hm.autoCastPhase = AutoCastPhase::StartRequested;
            hm.waitingChargeComplete = false;
            hm.chargeComplete = false;
            hm.castingElapsedSecs = 0.f;
            _aa.Secs(hand) = 0.f;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} post-cast spurious → restart",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand)) result.restartLeft = true;
            else result.restartRight = true;
            return result;
        }

        // Interrupt after 0.2s: treat as real → finish
        const float finished = FinishHand(hand);
        if (IsLeft(hand))
            result.finishedLeft = finished;
        else
            result.finishedRight = finished;

        return result;
    }
}
