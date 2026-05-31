#include <utility>

#include "Config/ConfigAdapter.h"
#include "Domain/CasterUtil.h"
#include "Shared/InventoryUtil.h"
#include "Shared/SpellClassify.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Shared/Hand.h"

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

    bool MagicState::IsCasterIdleForExpectedSpell(Hand hand, const RE::SpellItem* expectedSpell) const {
        auto* player = GetPlayer();
        if (!player) return true;

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

        const auto* caster = GetMagicCaster(player, src);
        if (!caster) return true;

        const auto* current = caster->currentSpell;
        const auto state = std::to_underlying(caster->state.get());

        return current != expectedSpell && state == 0;
    }

    void MagicState::ResetAutoCastStartState(Hand hand) {
        auto& hm = ModeFor(hand);
        hm.waitingBeginCast = false;
        hm.beginCastWaitSecs = 0.f;
        hm.beginCastRetries = 0;
        hm.waitingEnableBumperSecs = 0.f;
        hm.startRequestSecs = 0.f;
        hm.stalledCastSecs = 0.f;
    }

    void MagicState::ConfirmAutoCastStarted(Hand hand) {
        auto& hm = ModeFor(hand);

        if (DelayFor(hand).pending) {
            MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={} ignored - delayed start pending",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        if (!_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={} ignored - aaHeld=false",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        hm.autoCastPhase = AutoCastPhase::Casting;
        hm.waitingAutoAfterEquip = false;
        hm.waitingChargeComplete = true;
        hm.waitingBeginCast = false;
        hm.beginCastWaitSecs = 0.f;
        hm.beginCastRetries = 0;
        hm.startRequestSecs = 0.f;
        hm.stalledCastSecs = 0.f;
        hm.sawBeginCastEvent = false;
        CancelDelayedStart(hand);

        MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={}", IsLeft(hand) ? "Left" : "Right");
    }

    bool MagicState::HasRealCastStarted(Hand hand, const RE::SpellItem* expectedSpell) const {
        auto* player = GetPlayer();
        if (!player) return false;

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

        const auto* caster = GetMagicCaster(player, src);
        if (!caster) return false;

        const auto* current = caster->currentSpell;
        const auto state = std::to_underlying(caster->state.get());

        MAGIC_DEBUG_LOG(
            "[State] HasRealCastStarted probe: hand={} caster={} current={:#010x} state={} expected={:#010x}",
            IsLeft(hand) ? "L" : "R", caster != nullptr, current ? current->GetFormID() : 0u, state,
            expectedSpell ? expectedSpell->GetFormID() : 0u);

        if (current == expectedSpell) return true;

        if (current != nullptr && state != 0) return true;

        return false;
    }

    bool MagicState::HasDualCastStarted(const RE::SpellItem* expectedSpell) const {
        auto* player = GetPlayer();
        if (!player || !expectedSpell) return false;

        auto* leftCaster = GetMagicCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = GetMagicCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        auto started = [&](const RE::MagicCaster* caster) {
            if (!caster) return false;
            const auto* current = caster->currentSpell;
            const auto state = std::to_underlying(caster->state.get());

            if (current == expectedSpell) return true;
            if (current != nullptr && state != 0) return true;

            return false;
        };

        return started(leftCaster) || started(rightCaster);
    }

    bool MagicState::RequestAutoAttackStart(Hand hand, bool clearWaitAfterEquip) {
        auto& hm = ModeFor(hand);
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif

        MAGIC_DEBUG_LOG(
            "[FLOW] RequestAutoAttackStart: hand={} ENTRY clearWaitAfterEquip={} | "
            "autoActive={} holdActive={} wantAutoAttack={} finished={} aaHeld={} phase={}",
            handStr, clearWaitAfterEquip, hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished, _aa.Held(hand),
            static_cast<int>(hm.autoCastPhase));

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) || hm.finished) {
            MAGIC_DEBUG_LOG(
                "[FLOW] RequestAutoAttackStart: hand={} REJECT - neither auto nor (hold+want) or already finished",
                handStr);
            return false;
        }

        if (_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} REJECT - aaHeld already true", handStr);
            return false;
        }

        MAGIC_DEBUG_LOG("[FLOW] RequestAutoAttackStart: hand={} ACCEPTED, setting aaHeld=true and phase=StartRequested",
                        handStr);

        _aa.Held(hand) = true;
        _aa.Secs(hand) = 0.f;

        if (clearWaitAfterEquip) {
            hm.waitingAutoAfterEquip = false;
        }

        hm.waitingBeginCast = true;
        hm.beginCastWaitSecs = 0.f;
        hm.startRequestSecs = 0.f;
        hm.stalledCastSecs = 0.f;
        hm.autoCastPhase = AutoCastPhase::StartRequested;
        hm.sawBeginCastEvent = false;
        hm.casterInterruptPending = false;

        MAGIC_DEBUG_LOG(
            "[FLOW] RequestAutoAttackStart: hand={} EXIT - "
            "aaHeld={} phase={} waitingBeginCast={} sawBeginCast={} stalledSecs={:.3f} retries={}",
            handStr, _aa.Held(hand), static_cast<int>(hm.autoCastPhase), hm.waitingBeginCast, hm.sawBeginCastEvent,
            hm.stalledCastSecs, hm.beginCastRetries);

        return true;
    }

    AttackEnabledResult MagicState::NotifyAttackEnabled() {
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: ignored - not active");
            return {};
        }
        _session.attackEnabled = true;

        MAGIC_DEBUG_LOG(
            "[State] NotifyAttackEnabled: left.waitingAutoAfterEquip={} right.waitingAutoAfterEquip={} "
            "aaHeldLeft={} aaHeldRight={}",
            _left.waitingAutoAfterEquip, _right.waitingAutoAfterEquip, _aa.heldLeft, _aa.heldRight);

        AttackEnabledResult result;

        using enum Hand;
        auto tryStart = [&](Hand hand, bool& dispatchFlag) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingAutoAfterEquip) {
                MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: {} skipped - waitingAutoAfterEquip=false",
                                IsLeft(hand) ? "Left" : "Right");
                return;
            }
            hm.waitingAutoAfterEquip = false;
            if (!(hm.autoActive || hm.wantAutoAttack)) {
                MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: {} skipped - autoActive={} wantAutoAttack={}",
                                IsLeft(hand) ? "Left" : "Right", hm.autoActive, hm.wantAutoAttack);
                return;
            }

            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: starting {} auto attack", IsLeft(hand) ? "Left" : "Right");

            dispatchFlag = RequestAutoAttackStart(hand, true);
        };

        tryStart(Left, result.dispatchLeft);
        tryStart(Right, result.dispatchRight);
        return result;
    }

    void MagicState::OnBeginCast(Hand hand) {
        auto& hm = ModeFor(hand);

        MAGIC_DEBUG_LOG("[State] OnBeginCast: hand={} waitingBeginCast={} retries={}", IsLeft(hand) ? "Left" : "Right",
                        hm.waitingBeginCast, hm.beginCastRetries);

        if (!hm.waitingBeginCast) {
            MAGIC_DEBUG_LOG("[State] OnBeginCast: hand={} NOT waiting - autoActive={} chargeComplete={} finished={}",
                            IsLeft(hand) ? "Left" : "Right", hm.autoActive, hm.chargeComplete, hm.finished);
            return;
        }

        hm.sawBeginCastEvent = true;
        hm.beginCastWaitSecs = 0.f;

        MAGIC_DEBUG_LOG("[State] OnBeginCast: hand={} saw event only - waiting for caster confirmation",
                        IsLeft(hand) ? "Left" : "Right");
    }

    StateExitResult MagicState::OnCastStop() {
        using enum Hand;

        StateExitResult result{};
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] OnCastStop: ignored - not active");
            return result;
        }

        MAGIC_DEBUG_LOG(
            "[State] OnCastStop: castStopsToSkip={} isDualCasting={} "
            "left.autoActive={} left.chargeComplete={} left.finished={} "
            "right.autoActive={} right.chargeComplete={} right.finished={} "
            "left.holdFired={} right.holdFired={}",
            _cast.castStopsToSkip, _session.isDualCasting, _left.autoActive, _left.chargeComplete, _left.finished,
            _right.autoActive, _right.chargeComplete, _right.finished, _left.holdFiredAndWaitingCastStop,
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

        if (_cast.castStopsToSkip > 0) {
            --_cast.castStopsToSkip;
            const bool isLastSkip = (_cast.castStopsToSkip == 0);

            MAGIC_DEBUG_LOG("[State] OnCastStop: SKIPPING cast stop (remaining={}), isLastSkip={}",
                            _cast.castStopsToSkip, isLastSkip);

            if (isLastSkip) {
                if (const bool isTwoHanded =
                        [&]() {
                            if (!_session.active || _session.activeSlot < 0) return false;
                            if (_session.modeSpellRight != nullptr) return false;
                            return SpellClassify::IsTwoHandedSpell(_session.modeSpellLeft);
                        }();
                    isTwoHanded)
                    return result;

                auto stopAndDelay = [&](Hand h) {
                    auto& hm = ModeFor(h);
                    if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
                        CancelDelayedStart(h);

                        if (_aa.Held(h)) {
                            const float held = (_aa.Secs(h) > 0.f) ? _aa.Secs(h) : 0.1f;

                            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}",
                                            IsLeft(h) ? "Left" : "Right", held);

                            (IsLeft(h) ? result.leftAttack : result.rightAttack) = StopDispatchIntent{held};
                            _aa.Held(h) = false;
                            _aa.Secs(h) = 0.f;
                        }

                        hm.waitingBeginCast = true;
                        hm.beginCastWaitSecs = 0.f;
                        hm.beginCastRetries = 0;
                        ScheduleDelayedStart(h);

                        MAGIC_DEBUG_LOG("[State] OnCastStop: scheduled delayed start for hand={}",
                                        IsLeft(h) ? "Left" : "Right");
                    }
                };

                stopAndDelay(Left);
                stopAndDelay(Right);
            }

            return result;
        }

        if (_session.isDualCasting) {
            if (_session.dualCastSkipCastStops > 0) {
                --_session.dualCastSkipCastStops;
                return result;
            }

            if (!_left.chargeComplete && !_right.chargeComplete && !_left.holdFiredAndWaitingCastStop &&
                !_right.holdFiredAndWaitingCastStop)
                return result;

            const float finishedL = FinishHand(Left);
            if (finishedL != -1.f) result.leftAttack = StopDispatchIntent{finishedL};

            const float finishedR = FinishHand(Right);
            if (finishedR != -1.f) result.rightAttack = StopDispatchIntent{finishedR};

            _session.isDualCasting = false;
            merge(TryFinalizeExit());
            return result;
        }

        if (_left.autoActive && !_left.finished && _left.chargeComplete) {
            if (_left.pressAutocast) {
                _left.autoActive = false;
                _left.chargeComplete = false;
                _left.waitingChargeComplete = false;
                _left.pressAutocast = false;
            } else {
                const float finishedL = FinishHand(Left);
                if (finishedL != -1.f) result.leftAttack = StopDispatchIntent{finishedL};
            }
        } else if (_left.autoActive && !_left.finished && !_left.chargeComplete) {
            MAGIC_DEBUG_LOG(
                "[State] OnCastStop: Left autoActive but chargeComplete=false - waitingChargeComplete={} aaHeld={}",
                _left.waitingChargeComplete, _aa.heldLeft);
        }

        if (_right.autoActive && !_right.finished && _right.chargeComplete) {
            if (_right.pressAutocast) {
                _right.autoActive = false;
                _right.chargeComplete = false;
                _right.waitingChargeComplete = false;
                _right.pressAutocast = false;
            } else {
                const float finishedR = FinishHand(Right);
                if (finishedR != -1.f) result.rightAttack = StopDispatchIntent{finishedR};
            }
        } else if (_right.autoActive && !_right.finished && !_right.chargeComplete) {
            MAGIC_DEBUG_LOG(
                "[State] OnCastStop: Right autoActive but chargeComplete=false - waitingChargeComplete={} aaHeld={}",
                _right.waitingChargeComplete, _aa.heldRight);
        }

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

    CastInterruptResult MagicState::OnCastInterrupt() {
        CastInterruptResult result;
        if (!_session.active) return result;

        MAGIC_DEBUG_LOG("[State] OnCastInterrupt: firstInterrupt={} left.autoActive={} right.autoActive={}",
                        _session.firstInterrupt, _left.autoActive, _right.autoActive);

        if (_session.firstInterrupt == 0) {
            ++_session.firstInterrupt;
            MAGIC_DEBUG_LOG("[State] OnCastInterrupt: first interrupt - ignoring");
            return result;
        }

        if (_session.wasHandsDown && _session.firstInterrupt == 1) {
            ++_session.firstInterrupt;
            MAGIC_DEBUG_LOG("[State] OnCastInterrupt: weapon-draw interrupt - ignoring");
            using enum Hand;
            auto resetPhase = [&](Hand h) {
                auto& hm = ModeFor(h);
                if (_aa.Held(h) && hm.autoCastPhase == AutoCastPhase::Casting) {
                    hm.autoCastPhase = AutoCastPhase::StartRequested;
                    hm.waitingBeginCast = true;
                    hm.beginCastWaitSecs = 0.f;
                    hm.startRequestSecs = 0.f;
                    hm.stalledCastSecs = 0.f;
                    hm.sawBeginCastEvent = false;
                    hm.casterInterruptPending = false;
                    MAGIC_DEBUG_LOG("[State] OnCastInterrupt: weapon-draw -> reset {} to StartRequested",
                                    IsLeft(h) ? "Left" : "Right");
                }
            };
            resetPhase(Left);
            resetPhase(Right);

            return result;
        }

        ++_session.firstInterrupt;
        using enum Hand;
        bool anyFinished = false;

        if (_left.autoActive && !_left.finished && !_left.waitingBeginCast) {
            result.finishedLeft = FinishHand(Left);
            if (result.finishedLeft != -1.f) anyFinished = true;
        }
        if (_right.autoActive && !_right.finished && !_right.waitingBeginCast) {
            result.finishedRight = FinishHand(Right);
            if (result.finishedRight != -1.f) anyFinished = true;
        }

        if (anyFinished) _session.isDualCasting = false;
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

    PumpAutomaticHandResult MagicState::PumpAutomaticHand(Hand hand) {
        PumpAutomaticHandResult result{};

        auto& hm = ModeFor(hand);
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif

        if (!hm.autoActive || !hm.waitingChargeComplete) {
            return result;
        }

        auto* player = GetPlayer();
        if (!player || !_session.active || _session.activeSlot < 0) {
            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} no player/session -> FinishHand", handStr);
            const float finished = FinishHand(hand);
            if (finished != -1.f) result.attack = StopDispatchIntent{finished};
            return result;
        }

        const auto id = Slots::GetSlotSpell(_session.activeSlot, hand);
        if (id == 0) {
            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} spell id=0 -> FinishHand", handStr);
            const float finished = FinishHand(hand);
            if (finished != -1.f) result.attack = StopDispatchIntent{finished};
            return result;
        }

        const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
        if (!spell) {
            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} spell lookup failed -> FinishHand", handStr);
            const float finished = FinishHand(hand);
            if (finished != -1.f) result.attack = StopDispatchIntent{finished};
            return result;
        }

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

        const auto* caster = GetMagicCaster(player, src);
#ifdef DEBUG
        const auto casterCurrent = caster && caster->currentSpell ? caster->currentSpell->GetFormID() : 0u;
        const auto casterState = caster ? std::to_underlying(caster->state.get()) : -1;
#endif

        MAGIC_DEBUG_LOG(
            "[FLOW] PumpAutomaticHand hand={} phase={} aaHeld={} secs={:.2f} | "
            "spell={:#010x} | casterCurrent={:#010x} casterState={}",
            handStr, static_cast<int>(hm.autoCastPhase), _aa.Held(hand), _aa.Secs(hand), id, casterCurrent,
            casterState);

        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            if (_session.isDualCasting) {
                const auto* expected = _session.modeSpellLeft ? _session.modeSpellLeft : _session.modeSpellRight;

                if (HasDualCastStarted(expected)) {
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpAutomaticHand hand={} StartRequested -> dual cast confirmed via shared caster",
                        handStr);

                    ConfirmAutoCastStarted(Hand::Left);
                    ConfirmAutoCastStarted(Hand::Right);
                } else {
                    MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} StartRequested -> dual caster NOT started yet",
                                    handStr);
                }
            } else {
                const bool casterStarted = spell && HasRealCastStarted(hand, spell);
                if (casterStarted) {
                    MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} StartRequested -> confirmed via caster probe",
                                    handStr);
                    ConfirmAutoCastStarted(hand);
                } else {
                    MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} StartRequested -> caster NOT started yet",
                                    handStr);
                }
            }
        }

        if (hm.autoCastPhase == AutoCastPhase::Casting && hm.casterInterruptPending) {
            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} Casting + interruptPending -> FinishHand", handStr);
            hm.casterInterruptPending = false;
            const float finished = FinishHand(hand);
            if (finished != -1.f) result.attack = StopDispatchIntent{finished};
            return result;
        }

        if (!IsChargeComplete(caster, spell)) {
            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} charge NOT complete - chargeTime={:.3f}", handStr,
                            spell->GetChargeTime());
            return result;
        }

        hm.autoCastPhase = AutoCastPhase::WaitingChargeRelease;

        MAGIC_DEBUG_LOG(
            "[FLOW] PumpAutomaticHand hand={} CHARGE COMPLETE -> phase=WaitingChargeRelease, stopping press", handStr);

        hm.waitingChargeComplete = false;
        hm.chargeComplete = true;

        if (_aa.Held(hand)) {
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[FLOW] PumpAutomaticHand hand={} emitting stopAttack heldSecs={:.3f}", handStr, held);

            result.attack = StopDispatchIntent{held};
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }

        return result;
    }

    PumpAutoStartFallbackResult MagicState::PumpAutoStartFallback(Hand hand, float dt) {
        PumpAutoStartFallbackResult result{};

        auto& hm = ModeFor(hand);
        if (!_session.active || hm.finished) return result;
        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return result;

#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif
        const float add = dt > 0.f ? dt : 0.f;

        const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, hand) : 0;
        const auto* expectedSpell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;

        switch (hm.autoCastPhase) {
            case AutoCastPhase::Idle:
            case AutoCastPhase::Done:
            case AutoCastPhase::WaitingChargeRelease:
            case AutoCastPhase::Casting:
                return result;

            case AutoCastPhase::WaitingAttackEnable: {
                hm.waitingEnableBumperSecs += add;

                constexpr float kFallbackDelay = 0.25f;
                if (hm.waitingEnableBumperSecs >= kFallbackDelay) {
                    MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} fallback start after {:.3f}s", handStr,
                                    hm.waitingEnableBumperSecs);

                    result.startAttack = RequestAutoAttackStart(hand, false);
                }
                return result;
            }

            case AutoCastPhase::StartRequested: {
                hm.startRequestSecs += add;

                MAGIC_DEBUG_LOG(
                    "[FLOW] PumpAutoStartFallback[StartRequested] hand={} aaHeld={} startRequestSecs={:.3f} "
                    "stalledCastSecs={:.3f} retries={} sawBeginCast={} casterInterruptPending={}",
                    handStr, _aa.Held(hand), hm.startRequestSecs, hm.stalledCastSecs, hm.beginCastRetries,
                    hm.sawBeginCastEvent, hm.casterInterruptPending);

                if (_session.isDualCasting) {
                    const auto* expected = _session.modeSpellLeft ? _session.modeSpellLeft : _session.modeSpellRight;

                    if (HasDualCastStarted(expected)) {
                        MAGIC_DEBUG_LOG(
                            "[FLOW] PumpAutoStartFallback[StartRequested] dual cast started -> confirming both hands");

                        ConfirmAutoCastStarted(Hand::Left);
                        ConfirmAutoCastStarted(Hand::Right);

                        return result;
                    }
                }

                if (hm.casterInterruptPending) {
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpAutoStartFallback[StartRequested] hand={} caster interrupt pending -> FinishHand",
                        handStr);
                    hm.casterInterruptPending = false;
                    hm.autoCastPhase = AutoCastPhase::Done;

                    if (_aa.Held(hand)) {
                        const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
                        result.stopAttack = StopDispatchIntent{held};
                        _aa.Held(hand) = false;
                        _aa.Secs(hand) = 0.f;
                    }
                    (void)FinishHand(hand);
                    return result;
                }

                if (expectedSpell && HasRealCastStarted(hand, expectedSpell)) {
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpAutoStartFallback[StartRequested] hand={} HasRealCastStarted=true -> Confirm",
                        handStr);
                    ConfirmAutoCastStarted(hand);
                    return result;
                }

                const bool casterIdle =
                    _aa.Held(hand) && expectedSpell && IsCasterIdleForExpectedSpell(hand, expectedSpell);
                if (casterIdle) {
#ifdef DEBUG
                    const float prevStall = hm.stalledCastSecs;
#endif
                    hm.stalledCastSecs += add;
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpAutoStartFallback[StartRequested] hand={} caster IDLE -> stalledCastSecs {:.3f} -> "
                        "{:.3f}",
                        handStr, prevStall, hm.stalledCastSecs);
                } else {
                    if (hm.stalledCastSecs > 0.f) {
                        MAGIC_DEBUG_LOG(
                            "[FLOW] PumpAutoStartFallback[StartRequested] hand={} caster NOT idle -> stalledCastSecs "
                            "reset (was {:.3f})",
                            handStr, hm.stalledCastSecs);
                    }
                    hm.stalledCastSecs = 0.f;
                }

                constexpr float kStallTimeout = 0.20f;
                constexpr int kMaxRetries = 3;

                if (hm.stalledCastSecs < kStallTimeout) {
                    return result;
                }

                MAGIC_DEBUG_LOG(
                    "[FLOW] PumpAutoStartFallback[StartRequested] hand={} STALL TIMEOUT ({:.3f} >= {:.3f}) "
                    "retries={}/{}",
                    handStr, hm.stalledCastSecs, kStallTimeout, hm.beginCastRetries, kMaxRetries);

                hm.stalledCastSecs = 0.f;
                hm.startRequestSecs = 0.f;

                if (hm.beginCastRetries >= kMaxRetries) {
                    MAGIC_DEBUG_LOG("[FLOW] PumpAutoStartFallback[StartRequested] hand={} MAX RETRIES -> FinishHand",
                                    handStr);
                    hm.autoCastPhase = AutoCastPhase::Done;

                    const float finished = FinishHand(hand);
                    if (finished != -1.f) {
                        result.stopAttack = StopDispatchIntent{finished};
                    }
                    return result;
                }

                ++hm.beginCastRetries;
                MAGIC_DEBUG_LOG("[FLOW] PumpAutoStartFallback[StartRequested] hand={} RETRY {} - stop+reschedule",
                                handStr, hm.beginCastRetries);

                if (_aa.Held(hand)) {
                    const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
                    result.stopAttack = StopDispatchIntent{held};
                    MAGIC_DEBUG_LOG(
                        "[FLOW] PumpAutoStartFallback[StartRequested] hand={} emitting stopAttack heldSecs={:.3f}",
                        handStr, held);
                    _aa.Held(hand) = false;
                    _aa.Secs(hand) = 0.f;
                }

                hm.sawBeginCastEvent = false;
                ScheduleDelayedStart(hand);

                MAGIC_DEBUG_LOG(
                    "[FLOW] PumpAutoStartFallback[StartRequested] hand={} scheduled new delayed start (retries now={})",
                    handStr, hm.beginCastRetries);

                return result;
            }
        }

        return result;
    }

    DelayedStartsResult MagicState::PumpDelayedStarts(float dt) {
        DelayedStartsResult result{};

        if (!_session.active) {
            CancelAllDelayedStarts();
            return result;
        }

        auto pumpOne = [&](Hand h, bool& dispatchFlag) {
            auto& d = DelayFor(h);
#ifdef DEBUG
            const char* handStr = IsLeft(h) ? "Left" : "Right";
#endif

            if (!d.pending) {
                return;
            }

#ifdef DEBUG
            const float prevSecs = d.secs;
#endif
            d.secs += dt > 0.f ? dt : 0.f;

            MAGIC_DEBUG_LOG(
                "[FLOW] PumpDelayedStarts::pumpOne hand={} pending=true secs_prev={:.3f} secs_now={:.3f} "
                "threshold={:.3f} dt={:.3f}",
                handStr, prevSecs, d.secs, kDelayedStartSec, dt);

            if (d.secs < kDelayedStartSec) {
                MAGIC_DEBUG_LOG("[FLOW] PumpDelayedStarts::pumpOne hand={} NOT elapsed yet, waiting", handStr);
                return;
            }

            MAGIC_DEBUG_LOG("[FLOW] PumpDelayedStarts::pumpOne hand={} DELAY ELAPSED, clearing flags and dispatching",
                            handStr);

            d.pending = false;
            d.secs = 0.f;

#ifdef DEBUG
            auto& hm = ModeFor(h);
#endif

            MAGIC_DEBUG_LOG(
                "[FLOW] PumpDelayedStarts: hand={} state before RequestAutoAttackStart: "
                "autoActive={} holdActive={} wantAutoAttack={} finished={} "
                "aaHeld={} phase={} waitingBeginCast={} sawBeginCast={}",
                handStr, hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished, _aa.Held(h),
                static_cast<int>(hm.autoCastPhase), hm.waitingBeginCast, hm.sawBeginCastEvent);

            dispatchFlag = RequestAutoAttackStart(h, false);

            MAGIC_DEBUG_LOG(
                "[FLOW] PumpDelayedStarts: hand={} after RequestAutoAttackStart: dispatchFlag={} "
                "aaHeld={} phase={} secs={:.3f}",
                handStr, dispatchFlag, _aa.Held(h), static_cast<int>(hm.autoCastPhase), _aa.Secs(h));
        };

        using enum Hand;
        pumpOne(Left, result.dispatchLeft);
        pumpOne(Right, result.dispatchRight);

        if (result.dispatchLeft || result.dispatchRight) {
            MAGIC_DEBUG_LOG("[FLOW] PumpDelayedStarts: returning dispatchLeft={} dispatchRight={}", result.dispatchLeft,
                            result.dispatchRight);
        }

        return result;
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

        auto mergeDelayed = [&](const DelayedStartsResult& src) {
            result.startLeftAttack = result.startLeftAttack || src.dispatchLeft;
            result.startRightAttack = result.startRightAttack || src.dispatchRight;
        };

        auto mergeFallback = [&](Hand hand, const PumpAutoStartFallbackResult& src) {
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

        auto mergeAutoHand = [&](Hand hand, const PumpAutomaticHandResult& src) {
            if (!src.attack) return;

            if (IsLeft(hand)) {
                if (!result.stopLeftAttack) result.stopLeftAttack = StopDispatchIntent{src.attack->heldSecs};
            } else {
                if (!result.stopRightAttack) result.stopRightAttack = StopDispatchIntent{src.attack->heldSecs};
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

                    _left = {};
                    _right = {};
                    _aa.Reset();
                    _cast.Reset();
                    _session.attackEnabled = false;
                    _session.isDualCasting = false;
                    _session.dualCastSkipCastStops = 0;
                    _session.firstInterrupt = 0;
                    _session.activeTimeoutSecs = 0.f;
                    _session.modeSpellLeft = nullptr;
                    _session.modeSpellRight = nullptr;
                    CancelAllDelayedStarts();
                    ResetShoutState();
                    _session.active = false;
                    _session.activeSlot = -1;
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

            _left = {};
            _right = {};
            _aa.Reset();
            _cast.Reset();
            _session.attackEnabled = false;
            _session.isDualCasting = false;
            _session.dualCastSkipCastStops = 0;
            _session.firstInterrupt = 0;
            _session.activeTimeoutSecs = 0.f;
            _session.modeSpellLeft = nullptr;
            _session.modeSpellRight = nullptr;
            CancelAllDelayedStarts();
            ResetShoutState();
            _session.active = false;
            _session.activeSlot = -1;
            return result;
        }

        using enum Hand;

        mergeDelayed(PumpDelayedStarts(dt));
        mergeFallback(Left, PumpAutoStartFallback(Left, dt));
        mergeFallback(Right, PumpAutoStartFallback(Right, dt));
        mergeAutoHand(Left, PumpAutomaticHand(Left));
        mergeAutoHand(Right, PumpAutomaticHand(Right));
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
                        "L(auto={} charge={} charged={} waitEquip={} waitBegin={} aaHeld={} secs={:.1f}) "
                        "R(auto={} charge={} charged={} waitEquip={} waitBegin={} aaHeld={} secs={:.1f}) "
                        "attackEnabled={} castStopsToSkip={} firstInterrupt={}",
                        _session.activeSlot, _left.autoActive, _left.waitingChargeComplete, _left.chargeComplete,
                        _left.waitingAutoAfterEquip, _left.waitingBeginCast, _aa.heldLeft, _aa.secsLeft,
                        _right.autoActive, _right.waitingChargeComplete, _right.chargeComplete,
                        _right.waitingAutoAfterEquip, _right.waitingBeginCast, _aa.heldRight, _aa.secsRight,
                        _session.attackEnabled, _cast.castStopsToSkip, _session.firstInterrupt);
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

    void MagicState::OnCasterInterrupt(Hand hand, const RE::MagicItem* spell,
                                       bool
#ifdef DEBUG
                                           depleteEnergy
#endif
    ) {
        if (!_session.active) return;

        auto& hm = ModeFor(hand);
        const auto* expected = IsLeft(hand) ? _session.modeSpellLeft : _session.modeSpellRight;
        if (!expected) return;
        if (spell && spell != expected) return;

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return;
        if (hm.finished) return;

        if (!hm.sawBeginCastEvent) {
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} ignored - pre-BeginCast settling",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        if (hm.autoCastPhase == AutoCastPhase::WaitingChargeRelease) {
            MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} ignored - charge release phase",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }
        MAGIC_DEBUG_LOG("[State] OnCasterInterrupt: hand={} phase={} depleteEnergy={}", IsLeft(hand) ? "Left" : "Right",
                        static_cast<int>(hm.autoCastPhase), depleteEnergy);

        hm.casterInterruptPending = true;
    }
}