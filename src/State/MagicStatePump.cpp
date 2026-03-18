#include "Action.h"
#include "Config/Slots.h"
#include "InventoryUtil.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "State.h"

namespace IntegratedMagic {

    void MagicState::StartAutoAttack(Slots::Hand hand) {
#ifdef DEBUG
        spdlog::info("[State] StartAutoAttack: hand={}", IsLeft(hand) ? "Left" : "Right");
#endif
        _aa.Held(hand) = true;
        _aa.Secs(hand) = 0.f;
        detail::DispatchAttack(hand, 1.0f, 0.0f);
    }

    void MagicState::StopAutoAttack(Slots::Hand hand) {
        if (!_aa.Held(hand)) return;
        const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
#ifdef DEBUG
        spdlog::info("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);
#endif
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
#ifdef DEBUG
            spdlog::info("[State] NotifyAttackEnabled: ignored - not active");
#endif
            return;
        }
        _session.attackEnabled = true;

        if (auto* player = GetPlayer()) MagicAction::DisableSkipEquipVarsNow(player);
#ifdef DEBUG
        spdlog::info(
            "[State] NotifyAttackEnabled: left.waitingAutoAfterEquip={} right.waitingAutoAfterEquip={} "
            "aaHeldLeft={} aaHeldRight={}",
            _left.waitingAutoAfterEquip, _right.waitingAutoAfterEquip, _aa.heldLeft, _aa.heldRight);
#endif
        using enum Slots::Hand;
        auto tryStart = [&](Slots::Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.waitingAutoAfterEquip) return;
            hm.waitingAutoAfterEquip = false;
            if (!(hm.autoActive || hm.wantAutoAttack) || _aa.Held(hand)) return;
#ifdef DEBUG
            spdlog::info("[State] NotifyAttackEnabled: starting {} auto attack", IsLeft(hand) ? "Left" : "Right");
#endif
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
#ifdef DEBUG
        spdlog::info("[State] OnBeginCast: hand={} waitingBeginCast={} retries={}", IsLeft(hand) ? "Left" : "Right",
                     hm.waitingBeginCast, hm.beginCastRetries);
#endif
        if (!hm.waitingBeginCast) return;

        hm.waitingBeginCast = false;
        hm.beginCastWaitSecs = 0.f;
        hm.beginCastRetries = 0;
        CancelDelayedStart(hand);
#ifdef DEBUG
        spdlog::info("[State] OnBeginCast: hand={} -> cast confirmed, begin cast wait cleared",
                     IsLeft(hand) ? "Left" : "Right");
#endif
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
#ifdef DEBUG
            spdlog::info("[State] OnCastStop: ignored - not active");
#endif
            return;
        }
#ifdef DEBUG
        spdlog::info(
            "[State] OnCastStop: pendingSkipFirstCastStop={} isDualCasting={} "
            "left.autoActive={} left.chargeComplete={} left.finished={} "
            "right.autoActive={} right.chargeComplete={} right.finished={} "
            "left.holdFired={} right.holdFired={}",
            _cast.pendingSkipFirstCastStop, _session.isDualCasting, _left.autoActive, _left.chargeComplete,
            _left.finished, _right.autoActive, _right.chargeComplete, _right.finished,
            _left.holdFiredAndWaitingCastStop, _right.holdFiredAndWaitingCastStop);
#endif

        if (_cast.pendingSkipFirstCastStop) {
            _cast.pendingSkipFirstCastStop = false;
#ifdef DEBUG
            spdlog::info("[State] OnCastStop: SKIPPING first cast stop, scheduling delayed starts");
#endif
            auto stopAndDelay = [&](Slots::Hand h) {
                auto& hm = ModeFor(h);
                if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
                    CancelDelayedStart(h);
                    StopAutoAttack(h);
                    hm.waitingBeginCast = true;
                    hm.beginCastWaitSecs = 0.f;
                    hm.beginCastRetries = 0;
                    ScheduleDelayedStart(h);
#ifdef DEBUG
                    spdlog::info("[State] OnCastStop: scheduled delayed start for hand={}",
                                 IsLeft(h) ? "Left" : "Right");
#endif
                }
            };
            stopAndDelay(Left);
            stopAndDelay(Right);
            return;
        }

        if (_session.isDualCasting) {
            if (_session.dualCastSkipCastStops > 0) {
                --_session.dualCastSkipCastStops;
                return;
            }
            FinishHand(Left);
            FinishHand(Right);
            _session.isDualCasting = false;
            TryFinalizeExit();
            return;
        }

        if (_left.autoActive && !_left.finished && _left.chargeComplete) FinishHand(Left);
        if (_right.autoActive && !_right.finished && _right.chargeComplete) FinishHand(Right);
        if (_left.holdFiredAndWaitingCastStop && !_left.finished) FinishHand(Left);
        if (_right.holdFiredAndWaitingCastStop && !_right.finished) FinishHand(Right);
        TryFinalizeExit();
    }

    void MagicState::OnCastInterrupt() {
        if (!_session.active) return;
#ifdef DEBUG
        spdlog::info("[State] OnCastInterrupt: firstInterrupt={} left.autoActive={} right.autoActive={}",
                     _session.firstInterrupt, _left.autoActive, _right.autoActive);
#endif
        if (_session.firstInterrupt == 0) {
            ++_session.firstInterrupt;
#ifdef DEBUG
            spdlog::info("[State] OnCastInterrupt: first interrupt - ignoring");
#endif
            return;
        }
        if (_session.wasHandsDown && !_session.attackEnabled) {
            ++_session.firstInterrupt;
#ifdef DEBUG
            spdlog::info("[State] OnCastInterrupt: low hands interrupt - ignoring");
#endif
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
#ifdef DEBUG
        spdlog::info("[State] OnShoutStop: modeShoutID={:#010x} waitingStopEvent={}", _shout.modeShoutID,
                     _shout.waitingStopEvent);
#endif

        const auto ss = SpellSettingsDB::Get().GetOrCreate(_shout.modeShoutID);
        const bool isHold = (ss.mode == ActivationMode::Hold);
        const bool isAuto = (ss.mode == ActivationMode::Automatic);

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
#ifdef DEBUG
        spdlog::info("[State] PumpAutomaticHand: hand={} CHARGE COMPLETE - stopping auto attack",
                     IsLeft(hand) ? "Left" : "Right");
#endif
        hm.waitingChargeComplete = false;
        hm.chargeComplete = true;
        StopAutoAttack(hand);
    }

    void MagicState::PumpAutoStartFallback(Slots::Hand hand, float dt) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
        if (!_session.active) return;
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif
        if (hm.waitingAutoAfterEquip) {
            hm.waitingEnableBumperSecs += dt > 0.f ? dt : 0.f;

            if (constexpr float kFallbackDelay = 0.25f; hm.waitingEnableBumperSecs >= kFallbackDelay) {
#ifdef DEBUG
                spdlog::info("[State] PumpAutoStartFallback: hand={} FALLBACK after {:.3f}s", handStr,
                             hm.waitingEnableBumperSecs);
#endif
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
#ifdef DEBUG
        spdlog::info("[State] PumpAutoStartFallback: hand={} BeginCast timeout! retry={}/{} hasLimit={}", handStr,
                     hm.beginCastRetries, kMaxRetries, hasLimit);
#endif
        if (!hasLimit || hm.beginCastRetries < kMaxRetries) {
            ++hm.beginCastRetries;
            if (!DelayFor(hand).pending) {
                StopAutoAttack(hand);
                ScheduleDelayedStart(hand);
            }
        } else {
#ifdef DEBUG
            spdlog::info("[State] PumpAutoStartFallback: hand={} MAX RETRIES -> FinishHand", handStr);
#endif
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
#ifdef DEBUG
            spdlog::info(
                "[State] PumpDelayedStarts: hand={} delay elapsed! autoActive={} holdActive={} "
                "wantAutoAttack={} finished={}",
                IsLeft(h) ? "Left" : "Right", hm.autoActive, hm.holdActive, hm.wantAutoAttack, hm.finished);
#endif
            if ((hm.autoActive || (hm.holdActive && hm.wantAutoAttack)) && !hm.finished) {
#ifdef DEBUG
                spdlog::info("[State] PumpDelayedStarts: hand={} -> StartAutoAttack", IsLeft(h) ? "Left" : "Right");
#endif
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
#ifdef DEBUG
            spdlog::info("[State] PumpAutomatic: pendingPowerRestore -> RestoreSnapshot");
#endif
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
                const bool giveUp =
                    player->IsInCombat() || player->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kWantToDraw;
                if (_restore.sheatheAnimComplete || giveUp) {
#ifdef DEBUG
                    spdlog::info("[State] PumpAutomatic: pendingRestoreAfterSheathe -> restore (giveUp={})", giveUp);
#endif
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
#ifdef DEBUG
            spdlog::info("[State] PumpAutomatic: pendingRestore -> RestoreSnapshot + deactivate");
#endif
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

        if (!_session.active) return;

        if (ShouldForceInterrupt()) {
#ifdef DEBUG
            spdlog::info("[State] PumpAutomatic: ShouldForceInterrupt -> ForceExit");
#endif
            ForceExit();
            return;
        }

        _session.activeTimeoutSecs += dt > 0.f ? dt : 0.f;
        if (_session.activeTimeoutSecs > kMaxActiveTimeoutSecs) {
#ifdef DEBUG
            spdlog::info("[State] PumpAutomatic: TIMEOUT -> ForceExit");
#endif
            ForceExit();
            return;
        }

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.held && !_shout.finished &&
            (SpellSettingsDB::Get().GetOrCreate(_shout.modeShoutID).mode == ActivationMode::Automatic)) {
            constexpr float kPowerAutoDuration = 0.5f;
            _shout.powerAutoSecs += dt > 0.f ? dt : 0.f;
#ifdef DEBUG
            spdlog::info("[State] PumpAutomatic: power auto secs={:.3f}/{:.3f}", _shout.powerAutoSecs,
                         kPowerAutoDuration);
#endif
            if (_shout.powerAutoSecs >= kPowerAutoDuration) {
#ifdef DEBUG
                spdlog::info("[State] PumpAutomatic: power auto duration elapsed -> StopShoutPress + finish");
#endif
                StopShoutPress();
                _shout.finished = true;
                TryFinalizeExit();
            }
        }
    }
}