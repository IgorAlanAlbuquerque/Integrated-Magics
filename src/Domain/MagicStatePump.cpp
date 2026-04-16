#include <utility>

#include "Domain/InventoryUtil.h"
#include "Domain/SpellClassify.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Persistence/SpellSettingsDB.h"
#include "Shared/Hand.h"

namespace IntegratedMagic {
    PumpResult MagicState::PumpAutoAttack(float dt) {
        using enum Hand;
        const float add = dt > 0.f ? dt : 0.f;
        PumpResult result;

        if (_aa.heldLeft) {
            _aa.secsLeft += add;
            result.leftAttack = {Left, 1.0f, _aa.secsLeft};
        }
        if (_aa.heldRight) {
            _aa.secsRight += add;
            result.rightAttack = {Right, 1.0f, _aa.secsRight};
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

        // 🚨 NOVO: não confirmar durante delayed restart
        if (DelayFor(hand).pending) {
            MAGIC_DEBUG_LOG("[State] ConfirmAutoCastStarted: hand={} ignored - delayed start pending",
                            IsLeft(hand) ? "Left" : "Right");
            return;
        }

        // 🚨 NOVO: não confirmar se não está segurando ataque
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

        if (current == expectedSpell) return true;

        if (current != nullptr && state != 0) return true;

        return false;
    }

    bool MagicState::RequestAutoAttackStart(Hand hand, bool clearWaitAfterEquip) {
        auto& hm = ModeFor(hand);

        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) || hm.finished) {
            MAGIC_DEBUG_LOG(
                "[State] RequestAutoAttackStart: hand={} skipped autoActive={} holdActive={} wantAutoAttack={} "
                "finished={}",
                IsLeft(hand) ? "Left" : "Right", hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished);
            return false;
        }

        if (_aa.Held(hand)) {
            MAGIC_DEBUG_LOG("[State] RequestAutoAttackStart: hand={} skipped aaHeld=true",
                            IsLeft(hand) ? "Left" : "Right");
            return false;
        }

        MAGIC_DEBUG_LOG("[State] RequestAutoAttackStart: hand={} clearWaitAfterEquip={}",
                        IsLeft(hand) ? "Left" : "Right", clearWaitAfterEquip);

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

    void MagicState::OnCastStop() {
        using enum Hand;
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] OnCastStop: ignored - not active");

            return;
        }

        MAGIC_DEBUG_LOG(
            "[State] OnCastStop: castStopsToSkip={} isDualCasting={} "
            "left.autoActive={} left.chargeComplete={} left.finished={} "
            "right.autoActive={} right.chargeComplete={} right.finished={} "
            "left.holdFired={} right.holdFired={}",
            _cast.castStopsToSkip, _session.isDualCasting, _left.autoActive, _left.chargeComplete, _left.finished,
            _right.autoActive, _right.chargeComplete, _right.finished, _left.holdFiredAndWaitingCastStop,
            _right.holdFiredAndWaitingCastStop);

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
                    return;

                auto stopAndDelay = [&](Hand h) {
                    auto& hm = ModeFor(h);
                    if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
                        CancelDelayedStart(h);
                        if (_aa.Held(h)) {
                            const float held = (_aa.Secs(h) > 0.f) ? _aa.Secs(h) : 0.1f;

                            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}",
                                            IsLeft(h) ? "Left" : "Right", held);

                            if (_outbound.dispatchAttack) _outbound.dispatchAttack(h, 0.0f, held);
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
            return;
        }

        if (_session.isDualCasting) {
            if (_session.dualCastSkipCastStops > 0) {
                --_session.dualCastSkipCastStops;
                return;
            }
            if (!_left.chargeComplete && !_right.chargeComplete) {
                return;
            }
            FinishHand(Left);
            FinishHand(Right);
            _session.isDualCasting = false;
            TryFinalizeExit();
            return;
        }

        if (_left.autoActive && !_left.finished && _left.chargeComplete) {
            if (_left.pressAutocast) {
                _left.autoActive = false;
                _left.chargeComplete = false;
                _left.waitingChargeComplete = false;
                _left.pressAutocast = false;
            } else {
                FinishHand(Left);
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
                FinishHand(Right);
            }
        } else if (_right.autoActive && !_right.finished && !_right.chargeComplete) {
            MAGIC_DEBUG_LOG(
                "[State] OnCastStop: Right autoActive but chargeComplete=false - waitingChargeComplete={} aaHeld={}",
                _right.waitingChargeComplete, _aa.heldRight);
        }
        if (_left.holdFiredAndWaitingCastStop && !_left.finished) FinishHand(Left);
        if (_right.holdFiredAndWaitingCastStop && !_right.finished) FinishHand(Right);
        TryFinalizeExit();
    }

    void MagicState::OnCastInterrupt() {
        if (!_session.active) return;

        MAGIC_DEBUG_LOG("[State] OnCastInterrupt: firstInterrupt={} left.autoActive={} right.autoActive={}",
                        _session.firstInterrupt, _left.autoActive, _right.autoActive);

        if (_session.firstInterrupt == 0) {
            ++_session.firstInterrupt;
            MAGIC_DEBUG_LOG("[State] OnCastInterrupt: first interrupt - ignoring");
            return;
        }

        if (_session.wasHandsDown && _session.firstInterrupt == 1) {
            ++_session.firstInterrupt;
            MAGIC_DEBUG_LOG("[State] OnCastInterrupt: weapon-draw interrupt - ignoring");
            return;
        }

        ++_session.firstInterrupt;
        using enum Hand;
        bool anyFinished = false;
        if (_left.autoActive && !_left.finished && !_left.waitingBeginCast) {
            FinishHand(Left);
            anyFinished = true;
        }
        if (_right.autoActive && !_right.finished && !_right.waitingBeginCast) {
            FinishHand(Right);
            anyFinished = true;
        }
        if (anyFinished) _session.isDualCasting = false;
    }

    void MagicState::OnShoutStop() {
        if (!_session.active || _shout.modeShoutID == 0 || _shout.finished) return;
        if (_shout.isPower) return;

        MAGIC_DEBUG_LOG("[State] OnShoutStop: modeShoutID={:#010x} waitingStopEvent={}", _shout.modeShoutID,
                        _shout.waitingStopEvent);

        const auto ss = SpellSettingsDB::Get().Get(_shout.modeShoutID);
        if (!ss) return;
        const bool isHold = (ss->mode == ActivationMode::Hold);
        const bool isAuto = (ss->mode == ActivationMode::Automatic);

        if (isAuto || _shout.waitingStopEvent || isHold) {
            if (isHold && !_shout.waitingStopEvent)
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }
            _shout.waitingStopEvent = false;
            _shout.finished = true;
            TryFinalizeExit();
        }
    }

    void MagicState::PumpAutomaticHand(Hand hand) {
        auto& hm = ModeFor(hand);
        if (!hm.autoActive || !hm.waitingChargeComplete) return;

        auto* player = GetPlayer();
        if (!player || !_session.active || _session.activeSlot < 0) {
            MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} no player/session - finishing",
                            IsLeft(hand) ? "Left" : "Right");
            FinishHand(hand);
            return;
        }

        const auto id = Slots::GetSlotSpell(_session.activeSlot, hand);
        if (id == 0) {
            MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} spell id=0 - finishing",
                            IsLeft(hand) ? "Left" : "Right");
            FinishHand(hand);
            return;
        }

        const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
        if (!spell) {
            MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} spell lookup failed id={:#010x} - finishing",
                            IsLeft(hand) ? "Left" : "Right", id);
            FinishHand(hand);
            return;
        }

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

        const auto* caster = GetMagicCaster(player, src);
        if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
            const bool casterStarted = spell && HasRealCastStarted(hand, spell);
            if (casterStarted) {
                MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} confirmed by caster probe",
                                IsLeft(hand) ? "Left" : "Right");
                ConfirmAutoCastStarted(hand);
            }
        }
        if (!IsChargeComplete(caster, spell)) {
            if (_aa.Held(hand) && (static_cast<int>(_aa.Secs(hand) * 10.f) % 10 == 0)) {
                const auto* currentCasterSpell = caster ? caster->currentSpell : nullptr;
                const auto casterState = caster ? static_cast<int>(caster->state.get()) : -1;
                MAGIC_DEBUG_LOG(
                    "[State] PumpAutomaticHand: hand={} charge NOT complete - "
                    "spellID={:#010x} chargeTime={:.3f} "
                    "caster={} casterSpell={:#010x} casterState={} aaHeld={} heldSecs={:.1f}",
                    IsLeft(hand) ? "Left" : "Right", id, spell->GetChargeTime(), caster != nullptr,
                    currentCasterSpell ? currentCasterSpell->GetFormID() : 0u, casterState, _aa.Held(hand),
                    _aa.Secs(hand));
            }
            return;
        }
        hm.autoCastPhase = AutoCastPhase::WaitingChargeRelease;

        MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} CHARGE COMPLETE - stopping auto attack",
                        IsLeft(hand) ? "Left" : "Right");

        hm.waitingChargeComplete = false;
        hm.chargeComplete = true;
        hm.autoCastPhase = AutoCastPhase::WaitingChargeRelease;
        if (_aa.Held(hand)) {
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(hand, 0.0f, held);
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }
    }

    void MagicState::PumpAutoStartFallback(Hand hand, float dt) {
        auto& hm = ModeFor(hand);
        if (!_session.active || hm.finished) return;
        if (!(hm.autoActive || (hm.holdActive && hm.wantAutoAttack))) return;

        const char* handStr = IsLeft(hand) ? "Left" : "Right";
        const float add = dt > 0.f ? dt : 0.f;

        const auto id = (_session.activeSlot >= 0) ? Slots::GetSlotSpell(_session.activeSlot, hand) : 0;
        const auto* expectedSpell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;

        switch (hm.autoCastPhase) {
            case AutoCastPhase::Idle:
            case AutoCastPhase::Done:
            case AutoCastPhase::WaitingChargeRelease:
            case AutoCastPhase::Casting:
                return;

            case AutoCastPhase::WaitingAttackEnable: {
                hm.waitingEnableBumperSecs += add;

                constexpr float kFallbackDelay = 0.25f;
                if (hm.waitingEnableBumperSecs >= kFallbackDelay) {
                    MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} fallback start after {:.3f}s", handStr,
                                    hm.waitingEnableBumperSecs);
                    if (RequestAutoAttackStart(hand, false)) _outbound.dispatchAttack(hand, 1.0f, 0.0f);
                }
                return;
            }

            case AutoCastPhase::StartRequested: {
                hm.startRequestSecs += add;

                if (expectedSpell && expectedSpell && HasRealCastStarted(hand, expectedSpell)) {
                    ConfirmAutoCastStarted(hand);
                    return;
                }

                if (_aa.Held(hand) && expectedSpell && IsCasterIdleForExpectedSpell(hand, expectedSpell)) {
                    hm.stalledCastSecs += add;
                } else {
                    hm.stalledCastSecs = 0.f;
                }

                constexpr float kStallTimeout = 0.20f;
                constexpr int kMaxRetries = 3;

                if (hm.stalledCastSecs < kStallTimeout) {
                    return;
                }

                MAGIC_DEBUG_LOG(
                    "[State] PumpAutoStartFallback: hand={} stalled in StartRequested retries={}/{} "
                    "sawBeginCastEvent={}",
                    handStr, hm.beginCastRetries, kMaxRetries, hm.sawBeginCastEvent);

                hm.stalledCastSecs = 0.f;
                hm.startRequestSecs = 0.f;

                if (hm.beginCastRetries >= kMaxRetries) {
                    MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} MAX RETRIES -> FinishHand", handStr);
                    hm.autoCastPhase = AutoCastPhase::Done;
                    FinishHand(hand);
                    return;
                }

                ++hm.beginCastRetries;

                if (_aa.Held(hand)) {
                    const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
                    if (_outbound.dispatchAttack) {
                        _outbound.dispatchAttack(hand, 0.0f, held);
                    }
                    _aa.Held(hand) = false;
                    _aa.Secs(hand) = 0.f;
                }

                hm.sawBeginCastEvent = false;
                ScheduleDelayedStart(hand);
                return;
            }
        }
    }

    void MagicState::PumpDelayedStarts(float dt) {
        if (!_session.active) {
            CancelAllDelayedStarts();
            return;
        }

        auto pumpOne = [&](Hand h) {
            auto& d = DelayFor(h);
            if (!d.pending) return;

            d.secs += dt > 0.f ? dt : 0.f;
            if (d.secs < kDelayedStartSec) return;

            d.pending = false;
            d.secs = 0.f;

            auto& hm = ModeFor(h);

            MAGIC_DEBUG_LOG(
                "[State] PumpDelayedStarts: hand={} delay elapsed! autoActive={} holdActive={} wantAutoAttack={} "
                "finished={}",
                IsLeft(h) ? "Left" : "Right", hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished);

            if (RequestAutoAttackStart(h, false)) _outbound.dispatchAttack(h, 1.0f, 0.0f);
        };

        using enum Hand;
        pumpOne(Left);
        pumpOne(Right);
    }

    void MagicState::PumpAutomatic(float dt) {
        if (_restore.pendingPowerRestore) {
            if (_restore.pendingPowerRestoreDelaySecs > 0.f) {
                _restore.pendingPowerRestoreDelaySecs -= dt > 0.f ? dt : 0.f;
                return;
            }

            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingPowerRestore -> RestoreSnapshot");

            _restore.pendingPowerRestore = false;
            _restore.pendingPowerRestoreDelaySecs = 0.f;
            if (auto* player = GetPlayer()) {
                RestoreSnapshot(player);
                if (_outbound.reequipPrevExtraEquipped)
                    _outbound.reequipPrevExtraEquipped(player, _restore.prevExtraEquipped);
            }
            _restore.snapshot = {};
            return;
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
                    RestoreSnapshot(player);
                    if (_outbound.reequipPrevExtraEquipped)
                        _outbound.reequipPrevExtraEquipped(player, _restore.prevExtraEquipped);

                    _restore.snapshot = {};
                    ResetSessionState();
                }
            }
            return;
        }

        if (_restore.pendingRestore) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingRestore -> RestoreSnapshot + deactivate");

            _restore.pendingRestore = false;
            if (auto* player = GetPlayer()) {
                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }
                RestoreSnapshot(player);
                if (_outbound.reequipPrevExtraEquipped)
                    _outbound.reequipPrevExtraEquipped(player, _restore.prevExtraEquipped);
            }
            ResetSessionState();
            _restore.snapshot.valid = false;
            return;
        }

        using enum Hand;
        PumpDelayedStarts(dt);
        PumpAutoStartFallback(Left, dt);
        PumpAutoStartFallback(Right, dt);
        PumpAutomaticHand(Left);
        PumpAutomaticHand(Right);
        PumpSpellFireFinalize(dt);

        if (!_session.active) return;

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

            ForceExit();
            return;
        }

        _session.activeTimeoutSecs += dt > 0.f ? dt : 0.f;
        if (_session.activeTimeoutSecs > kMaxActiveTimeoutSecs) {
            MAGIC_DEBUG_LOG("[State] PumpAutomatic: TIMEOUT -> ForceExit");

            ForceExit();
            return;
        }

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.held && !_shout.finished) {
            const auto ss = SpellSettingsDB::Get().Get(_shout.modeShoutID);
            if (ss && ss->mode == ActivationMode::Automatic) {
                constexpr float kPowerAutoDuration = 0.2f;
                _shout.powerAutoSecs += dt > 0.f ? dt : 0.f;

                MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto secs={:.3f}/{:.3f}", _shout.powerAutoSecs,
                                kPowerAutoDuration);

                if (_shout.powerAutoSecs >= kPowerAutoDuration) {
                    MAGIC_DEBUG_LOG("[State] PumpAutomatic: power auto duration elapsed -> StopShoutPress + finish");

                    if (_shout.held) {
                        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                        if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                        _shout.held = false;
                        _shout.heldSecs = 0.f;
                    }
                    _shout.finished = true;
                    TryFinalizeExit();
                }
            }
        }
    }

    void MagicState::OnSpellFired(Hand hand) {
        if (!_session.active) return;

        auto& hm = ModeFor(hand);

        if (hm.autoActive && !hm.finished && hm.chargeComplete) {
            if (hm.pressAutocast) {
                hm.autoActive = false;
                hm.chargeComplete = false;
                hm.waitingChargeComplete = false;
                hm.pressAutocast = false;
                return;
            }

            if (_session.isDualCasting) {
                using enum IntegratedMagic::Hand;
                FinishHand(Left);
                FinishHand(Right);
                _session.isDualCasting = false;

                ScheduleSpellFireFinalize(Left);
                ScheduleSpellFireFinalize(Right);
            } else {
                FinishHand(hand);
                ScheduleSpellFireFinalize(hand);
            }
        }
    }

    void MagicState::ScheduleSpellFireFinalize(Hand hand) {
        auto& hm = ModeFor(hand);
        hm.waitingSpellFireFinalize = true;
        hm.spellFireFinalizeSecs = 0.f;
    }

    void MagicState::PumpSpellFireFinalize(float dt) {
        if (!_session.active) {
            _left.waitingSpellFireFinalize = false;
            _left.spellFireFinalizeSecs = 0.f;
            _right.waitingSpellFireFinalize = false;
            _right.spellFireFinalizeSecs = 0.f;
            return;
        }

        constexpr float kSpellFireFinalizeDelay = 0.7f;

        auto pumpOne = [&](Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingSpellFireFinalize) return;

            hm.spellFireFinalizeSecs += dt > 0.f ? dt : 0.f;
            if (hm.spellFireFinalizeSecs < kSpellFireFinalizeDelay) return;

            hm.waitingSpellFireFinalize = false;
            hm.spellFireFinalizeSecs = 0.f;

            TryFinalizeExit();
        };

        using enum Hand;
        pumpOne(Left);
        pumpOne(Right);
    }
}