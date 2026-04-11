#include "Action.h"
#include "InventoryUtil.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Persistence/SpellSettingsDB.h"
#include "State.h"
#include "State/SpellClassify.h"

namespace IntegratedMagic {

    void MagicState::StartAutoAttack(Slots::Hand hand) {
        MAGIC_DEBUG_LOG("[State] StartAutoAttack: hand={}", IsLeft(hand) ? "Left" : "Right");

        _aa.Held(hand) = true;
        _aa.Secs(hand) = 0.f;
        detail::DispatchAttack(hand, 1.0f, 0.0f);
    }

    void MagicState::StopAutoAttack(Slots::Hand hand) {
        if (!_aa.Held(hand)) return;
        const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

        MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

        detail::DispatchAttack(hand, 0.0f, held);
        _aa.Held(hand) = false;
        _aa.Secs(hand) = 0.f;
    }

    void MagicState::StopAllAutoAttack() {
        using enum Slots::Hand;
        StopAutoAttack(Left);
        StopAutoAttack(Right);
    }

    void MagicState::PumpAutoAttack(float dt) {
        using enum Slots::Hand;
        const float add = dt > 0.f ? dt : 0.f;
        if (_aa.heldLeft) {
            _aa.secsLeft += add;
            detail::DispatchAttack(Left, 1.0f, _aa.secsLeft);
        }
        if (_aa.heldRight) {
            _aa.secsRight += add;
            detail::DispatchAttack(Right, 1.0f, _aa.secsRight);
        }
        if (_shout.held) {
            _shout.heldSecs += add;
            detail::DispatchShout(1.0f, _shout.heldSecs);
        }
    }

    void MagicState::NotifyAttackEnabled() {
        if (!_session.active) {
            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: ignored - not active");

            return;
        }
        _session.attackEnabled = true;

        if (auto* player = GetPlayer()) MagicAction::DisableSkipEquipVarsNow(player);

        MAGIC_DEBUG_LOG(
            "[State] NotifyAttackEnabled: left.waitingAutoAfterEquip={} right.waitingAutoAfterEquip={} "
            "aaHeldLeft={} aaHeldRight={}",
            _left.waitingAutoAfterEquip, _right.waitingAutoAfterEquip, _aa.heldLeft, _aa.heldRight);

        using enum Slots::Hand;
        auto tryStart = [&](Slots::Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingAutoAfterEquip) return;
            hm.waitingAutoAfterEquip = false;
            if (!(hm.autoActive || hm.wantAutoAttack) || _aa.Held(hand)) return;

            MAGIC_DEBUG_LOG("[State] NotifyAttackEnabled: starting {} auto attack", IsLeft(hand) ? "Left" : "Right");

            StartAutoAttack(hand);
            if (hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) {
                hm.waitingBeginCast = true;
                hm.beginCastWaitSecs = 0.f;
            }
        };
        tryStart(Left);
        tryStart(Right);
    }

    void MagicState::OnBeginCast(Slots::Hand hand) {
        auto& hm = ModeFor(hand);

        MAGIC_DEBUG_LOG("[State] OnBeginCast: hand={} waitingBeginCast={} retries={}", IsLeft(hand) ? "Left" : "Right",
                        hm.waitingBeginCast, hm.beginCastRetries);

        if (!hm.waitingBeginCast) return;

        hm.waitingBeginCast = false;
        hm.beginCastWaitSecs = 0.f;
        hm.beginCastRetries = 0;
        CancelDelayedStart(hand);

        MAGIC_DEBUG_LOG("[State] OnBeginCast: hand={} -> cast confirmed, begin cast wait cleared",
                        IsLeft(hand) ? "Left" : "Right");

        using enum Slots::Hand;
        const auto other = IsLeft(hand) ? Right : Left;
        auto& otherHm = ModeFor(other);
        if (otherHm.waitingBeginCast) {
            otherHm.waitingBeginCast = false;
            otherHm.beginCastWaitSecs = 0.f;
            otherHm.beginCastRetries = 0;
            CancelDelayedStart(other);
        }
    }

    void MagicState::OnCastStop() {
        using enum Slots::Hand;
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

                auto stopAndDelay = [&](Slots::Hand h) {
                    auto& hm = ModeFor(h);
                    if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
                        CancelDelayedStart(h);
                        StopAutoAttack(h);
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
        if (_session.wasHandsDown && !_session.attackEnabled) {
            ++_session.firstInterrupt;

            MAGIC_DEBUG_LOG("[State] OnCastInterrupt: low hands interrupt - ignoring");

            return;
        }
        ++_session.firstInterrupt;
        using enum Slots::Hand;
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
            if (isHold && !_shout.waitingStopEvent) StopShoutPress();
            _shout.waitingStopEvent = false;
            _shout.finished = true;
            TryFinalizeExit();
        }
    }

    void MagicState::PumpAutomaticHand(Slots::Hand hand) {
        auto& hm = ModeFor(hand);
        if (!hm.autoActive || !hm.waitingChargeComplete) return;

        auto* player = GetPlayer();
        if (!player || !_session.active || _session.activeSlot < 0) {
            FinishHand(hand);
            return;
        }

        const auto id = Slots::GetSlotSpell(_session.activeSlot, hand);
        if (id == 0) {
            FinishHand(hand);
            return;
        }

        const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
        if (!spell) {
            FinishHand(hand);
            return;
        }

        const auto src =
            IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

        if (auto const* caster = MagicAction::GetCaster(player, src); !IsChargeComplete(caster, spell)) return;

        MAGIC_DEBUG_LOG("[State] PumpAutomaticHand: hand={} CHARGE COMPLETE - stopping auto attack",
                        IsLeft(hand) ? "Left" : "Right");

        hm.waitingChargeComplete = false;
        hm.chargeComplete = true;
        StopAutoAttack(hand);
    }

    void MagicState::PumpAutoStartFallback(Slots::Hand hand, float dt) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
        if (!_session.active) return;

        const char* handStr = IsLeft(hand) ? "Left" : "Right";

        if (hm.waitingAutoAfterEquip) {
            hm.waitingEnableBumperSecs += dt > 0.f ? dt : 0.f;

            if (constexpr float kFallbackDelay = 0.25f; hm.waitingEnableBumperSecs >= kFallbackDelay) {
                MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} FALLBACK after {:.3f}s", handStr,
                                hm.waitingEnableBumperSecs);

                hm.waitingAutoAfterEquip = false;
                if (!_aa.Held(hand)) {
                    StartAutoAttack(hand);
                    hm.waitingBeginCast = true;
                    hm.beginCastWaitSecs = 0.f;
                }
            }
            return;
        }

        if (!hm.waitingBeginCast) return;
        if (!_session.attackEnabled) {
            hm.beginCastWaitSecs = 0.f;
            return;
        }

        constexpr float kBeginCastTimeout = 0.1f;
        constexpr int kMaxRetries = 3;
        hm.beginCastWaitSecs += dt > 0.f ? dt : 0.f;
        if (hm.beginCastWaitSecs < kBeginCastTimeout) return;
        hm.beginCastWaitSecs = 0.f;

        const bool hasLimit = (hm.mode == Automatic);

        MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} BeginCast timeout! retry={}/{} hasLimit={}", handStr,
                        hm.beginCastRetries, kMaxRetries, hasLimit);

        if (!hasLimit || hm.beginCastRetries < kMaxRetries) {
            ++hm.beginCastRetries;
            if (!DelayFor(hand).pending) {
                StopAutoAttack(hand);
                ScheduleDelayedStart(hand);
            }
        } else {
            MAGIC_DEBUG_LOG("[State] PumpAutoStartFallback: hand={} MAX RETRIES -> FinishHand", handStr);

            hm.waitingBeginCast = false;
            FinishHand(hand);
        }
    }

    void MagicState::PumpDelayedStarts(float dt) {
        if (!_session.active) {
            CancelAllDelayedStarts();
            return;
        }

        auto pumpOne = [&](Slots::Hand h) {
            auto& d = DelayFor(h);
            if (!d.pending) return;
            d.secs += dt > 0.f ? dt : 0.f;
            if (d.secs < kDelayedStartSec) return;
            d.pending = false;
            d.secs = 0.f;

            auto& hm = ModeFor(h);

            MAGIC_DEBUG_LOG(
                "[State] PumpDelayedStarts: hand={} delay elapsed! autoActive={} holdActive={} "
                "wantAutoAttack={} finished={}",
                IsLeft(h) ? "Left" : "Right", hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished);

            if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
                MAGIC_DEBUG_LOG("[State] PumpDelayedStarts: hand={} -> StartAutoAttack", IsLeft(h) ? "Left" : "Right");

                StartAutoAttack(h);
                hm.waitingBeginCast = true;
                hm.beginCastWaitSecs = 0.f;
            }
        };

        using enum Slots::Hand;
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
                if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                    auto idx = BuildInventoryIndex(player);
                    ReequipPrevExtraEquipped(player, mgr, idx, _restore.prevExtraEquipped);
                }
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
                ;
                if (_restore.sheatheAnimComplete || giveUp) {
                    _restore.sheatheWaitSecs = 0.f;

                    MAGIC_DEBUG_LOG("[State] PumpAutomatic: pendingRestoreAfterSheathe -> restore (giveUp={})", giveUp);

                    _restore.pendingRestoreAfterSheathe = false;
                    _restore.sheatheAnimComplete = false;
                    RestoreSnapshot(player);
                    if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                        auto idx = BuildInventoryIndex(player);
                        ReequipPrevExtraEquipped(player, mgr, idx, _restore.prevExtraEquipped);
                    }
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
                StopShoutPress();
                RestoreSnapshot(player);
                if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                    auto idx = BuildInventoryIndex(player);
                    ReequipPrevExtraEquipped(player, mgr, idx, _restore.prevExtraEquipped);
                }
            }
            ResetSessionState();
            _restore.snapshot.valid = false;
            return;
        }

        using enum Slots::Hand;
        PumpDelayedStarts(dt);
        PumpAutoStartFallback(Left, dt);
        PumpAutoStartFallback(Right, dt);
        PumpAutomaticHand(Left);
        PumpAutomaticHand(Right);
        PumpSpellFireFinalize(dt);

        if (!_session.active) return;

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

                    StopShoutPress();
                    _shout.finished = true;
                    TryFinalizeExit();
                }
            }
        }
    }

    void MagicState::OnSpellFired(Slots::Hand hand) {
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
                FinishHand(Slots::Hand::Left);
                FinishHand(Slots::Hand::Right);
                _session.isDualCasting = false;

                ScheduleSpellFireFinalize(Slots::Hand::Left);
                ScheduleSpellFireFinalize(Slots::Hand::Right);
            } else {
                FinishHand(hand);
                ScheduleSpellFireFinalize(hand);
            }
        }
    }

    void MagicState::ScheduleSpellFireFinalize(Slots::Hand hand) {
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

        auto pumpOne = [&](Slots::Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingSpellFireFinalize) return;

            hm.spellFireFinalizeSecs += dt > 0.f ? dt : 0.f;
            if (hm.spellFireFinalizeSecs < kSpellFireFinalizeDelay) return;

            hm.waitingSpellFireFinalize = false;
            hm.spellFireFinalizeSecs = 0.f;

            TryFinalizeExit();
        };

        using enum Slots::Hand;
        pumpOne(Left);
        pumpOne(Right);
    }
}