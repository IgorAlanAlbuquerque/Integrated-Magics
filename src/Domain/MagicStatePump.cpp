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

        hm.pendingRestartNextFrame = false;
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

        // Only re-dispatch if the hand has been in StartRequested for at least 100ms, to prevent
        // double-dispatch when EnableBumper and a spurious interrupt arrive in the same frame.
        constexpr float kMinRedispatchSecs = 0.1f;
        auto tryRedispatch = [&](Hand hand, bool& dispatchFlag) {
            auto& hm = ModeFor(hand);
            if (hm.autoCastPhase != AutoCastPhase::StartRequested) return;
            if (hm.pendingRestartNextFrame) return;  // already scheduled
            if (hm.startRequestSecs < kMinRedispatchSecs) {
                MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} skip re-dispatch (too soon secs={:.3f})",
                                IsLeft(hand) ? "Left" : "Right", hm.startRequestSecs);
                return;
            }
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.startRequestSecs = 0.f;
            hm.pendingRestartNextFrame = true;
            dispatchFlag = true;
            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: hand={} → UP (DOWN next frame)", IsLeft(hand) ? "Left" : "Right");
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

        auto handlePressAutocast = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && hm.pressActive &&
                hm.autoCastPhase != AutoCastPhase::Done) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.autoCastPhase = AutoCastPhase::Done;
            }
        };

        auto handleAutoComplete = [&](Hand h) {
            auto& hm = ModeFor(h);
            if (hm.autoActive && !hm.finished && hm.chargeComplete && !hm.pressActive &&
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

        if (!_session.active || hm.finished) {
            hm.pendingRestartNextFrame = false;
            return result;
        }

        // Deferred restart: UP was sent last frame, send DOWN now to create rising edge
        if (hm.pendingRestartNextFrame) {
            hm.pendingRestartNextFrame = false;
            _aa.Held(hand) = true;
            _aa.Secs(hand) = 0.f;
            hm.startRequestSecs = 0.f;
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} deferred restart → DOWN",
                            IsLeft(hand) ? "Left" : "Right");
            result.startAttack = true;
            return result;
        }

        const bool isAutoOrHold = hm.autoActive || (hm.holdActive && hm.wantAutoAttack);
        if (!isAutoOrHold && !hm.holdFiredAndWaitingCastStop) {
            if (hm.autoCastPhase != AutoCastPhase::Idle && hm.autoCastPhase != AutoCastPhase::Done) {
                MAGIC_DEBUG_LOG(
                    "[FLOW] PumpCastPhase: hand={} UNEXPECTED SKIP phase={} autoActive={} holdActive={} wantAuto={} "
                    "holdFired={}",
                    IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), hm.autoActive, hm.holdActive,
                    hm.wantAutoAttack, hm.holdFiredAndWaitingCastStop);
            }
            return result;
        }

        if (hm.autoCastPhase == AutoCastPhase::Idle || hm.autoCastPhase == AutoCastPhase::Done) return result;

        auto* player = GetPlayer();
        if (!player) return result;

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
        const auto* caster = GetMagicCaster(player, src);
        if (!caster) return result;

        const auto castStateEnum = caster->state.get();
        const auto casterState = std::to_underlying(castStateEnum);

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

        const bool castIsStable =
            castStateEnum == RE::MagicCaster::State::kReady || castStateEnum >= RE::MagicCaster::State::kCharging;
        if (hm.autoCastPhase == AutoCastPhase::StartRequested && castIsStable) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested → Casting (state={})", handStr, casterState);
            ConfirmAutoCastStarted(hand);
        }

        // Periodic re-dispatch: send UP this frame, schedule DOWN for the next frame.
        // The behavior machine needs to see UP→DOWN as a rising edge on separate frames.
        constexpr float kRedispatchInterval = 0.15f;
        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            const float prev = hm.startRequestSecs;
            hm.startRequestSecs += dt > 0.f ? dt : 0.f;
            if (hm.startRequestSecs >= kRedispatchInterval &&
                static_cast<int>(hm.startRequestSecs / kRedispatchInterval) >
                    static_cast<int>(prev / kRedispatchInterval)) {
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} StartRequested for {:.2f}s → UP (DOWN next frame)",
                                IsLeft(hand) ? "L" : "R", hm.startRequestSecs);
                _aa.Held(hand) = false;
                _aa.Secs(hand) = 0.f;
                hm.pendingRestartNextFrame = true;
                result.releaseAttack = true;
            }
        }

        if (hm.autoCastPhase == AutoCastPhase::Casting) {
            hm.castingElapsedSecs += dt > 0.f ? dt : 0.f;
        }

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

        const bool autoModeEnded =
            hm.autoActive &&
            (hm.autoCastPhase == AutoCastPhase::Casting || hm.autoCastPhase == AutoCastPhase::WaitingChargeRelease) &&
            casterState == 0;
        const bool holdModeEnded = hm.holdFiredAndWaitingCastStop && casterState == 0;

        if (autoModeEnded || holdModeEnded) {
            MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} cast ended (state=0 phase={} holdFired={})", handStr,
                            static_cast<int>(hm.autoCastPhase), hm.holdFiredAndWaitingCastStop);

            if (hm.pressActive) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.autoCastPhase = AutoCastPhase::Done;
                MAGIC_DEBUG_LOG("[FLOW] PumpCastPhase: hand={} press-autocast done, staying equipped", handStr);
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
            MAGIC_DEBUG_LOG("[FLOW] PumpSpellFireFinalize: hand={} timer={:.3f}/{:.3f}",
                            IsLeft(hand) ? "Left" : "Right", hm.spellFireFinalizeSecs, kSpellFireFinalizeDelay);
            if (hm.spellFireFinalizeSecs < kSpellFireFinalizeDelay) return;

            MAGIC_DEBUG_LOG("[FLOW] PumpSpellFireFinalize: hand={} delay elapsed → TryFinalizeExit",
                            IsLeft(hand) ? "Left" : "Right");
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

        MAGIC_DEBUG_LOG(
            "[State] OnSpellFired: hand={} autoActive={} chargeComplete={} pressActive={} finished={} isDual={}",
            IsLeft(hand) ? "Left" : "Right", hm.autoActive, hm.chargeComplete, hm.pressActive, hm.finished,
            _session.isDualCasting);

        if (!(hm.autoActive && !hm.finished && hm.chargeComplete)) return result;

        if (hm.pressActive) {
            hm.autoActive = false;
            hm.chargeComplete = false;
            hm.waitingChargeComplete = false;
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
            if (src.releaseAttack) {
                if (IsLeft(hand))
                    result.releaseLeftAttack = true;
                else
                    result.releaseRightAttack = true;
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

        if (hm.autoCastPhase != AutoCastPhase::StartRequested && hm.autoCastPhase != AutoCastPhase::Casting)
            return result;

        MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} phase={} depleteEnergy={} castingSecs={:.3f}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase), depleteEnergy,
                        hm.castingElapsedSecs);

        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.pendingRestartNextFrame = true;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} pre-cast spurious → UP (DOWN next frame)",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand))
                result.releaseLeft = true;
            else
                result.releaseRight = true;
            return result;
        }

        constexpr float kSpuriousInterruptWindow = 0.2f;
        const bool isSpurious = (hm.castingElapsedSecs < kSpuriousInterruptWindow);

        if (isSpurious) {
            hm.autoCastPhase = AutoCastPhase::StartRequested;
            hm.waitingChargeComplete = false;
            hm.chargeComplete = false;
            hm.castingElapsedSecs = 0.f;
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            hm.pendingRestartNextFrame = true;
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} post-cast spurious → UP (DOWN next frame)",
                            IsLeft(hand) ? "Left" : "Right");
            if (IsLeft(hand))
                result.releaseLeft = true;
            else
                result.releaseRight = true;
            return result;
        }

        const float finished = FinishHand(hand);
        if (IsLeft(hand))
            result.finishedLeft = finished;
        else
            result.finishedRight = finished;

        return result;
    }
}
