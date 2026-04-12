#include <utility>

#include "Config/ConfigAdapter.h"
#include "Domain/Hand.h"
#include "Domain/InventoryUtil.h"
#include "Domain/SlotCostUtil.h"
#include "Domain/SpellClassify.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Persistence/SpellSettingsDB.h"

namespace IntegratedMagic {

    void MagicState::SetModeSpellsFromHand(Domain::Hand hand, RE::SpellItem* spell) {
        if (IsLeft(hand))
            _session.modeSpellLeft = spell;
        else
            _session.modeSpellRight = spell;
    }

    void MagicState::DisableHand(Domain::Hand hand) {
        MAGIC_DEBUG_LOG("[State] DisableHand: hand={}", IsLeft(hand) ? "Left" : "Right");

        StopAutoAttack(hand);
        ModeFor(hand) = {};
        ModeFor(hand).finished = true;
        SetModeSpellsFromHand(hand, nullptr);
    }

    void MagicState::FinishHand(Domain::Hand hand) {
        MAGIC_DEBUG_LOG("[State] FinishHand: hand={}", IsLeft(hand) ? "Left" : "Right");

        auto& hm = ModeFor(hand);
        hm.finished = true;
        hm.holdActive = false;
        hm.autoActive = false;
        hm.pressActive = false;
        hm.waitingAutoAfterEquip = false;
        hm.waitingChargeComplete = false;
        hm.holdFiredAndWaitingCastStop = false;
        hm.waitingBeginCast = false;
        hm.beginCastWaitSecs = 0.f;
        hm.beginCastRetries = 0;
        StopAutoAttack(hand);
        CancelDelayedStart(hand);
    }

    void MagicState::TogglePressHand(Domain::Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;
        hm.pressActive = !hm.pressActive;
        if (!hm.pressActive) FinishHand(hand);
    }

    void MagicState::EnterHand(Domain::Hand hand, const SpellSettings& ss) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
        hm = {};
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;

        const char* handStr = IsLeft(hand) ? "Left" : "Right";

        const bool skipAnim = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimation();
        switch (ss.mode) {
            case Hold:
                hm.holdActive = true;
                if (hm.wantAutoAttack) {
                    hm.waitingAutoAfterEquip = true;
                    hm.waitingEnableBumperSecs = 0.f;
                    hm.waitingBeginCast = true;
                    hm.beginCastWaitSecs = 0.f;
                    hm.beginCastRetries = 0;
                    _session.attackEnabled = false;
                    _cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown ? 2 : 1) : 0;
                }

                MAGIC_DEBUG_LOG(
                    "[State] EnterHand: hand={} mode=Hold wantAutoAttack={} "
                    "waitingAutoAfterEquip={} castStopsToSkip={}",
                    handStr, hm.wantAutoAttack, hm.waitingAutoAfterEquip, _cast.castStopsToSkip);

                break;
            case Automatic:
                hm.autoActive = true;
                hm.waitingChargeComplete = true;
                hm.waitingAutoAfterEquip = true;
                hm.wantAutoAttack = true;
                hm.waitingEnableBumperSecs = 0.f;
                hm.waitingBeginCast = true;
                hm.beginCastWaitSecs = 0.f;
                hm.beginCastRetries = 0;
                _session.attackEnabled = false;
                _cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown ? 2 : 1) : 0;

                MAGIC_DEBUG_LOG(
                    "[State] EnterHand: hand={} mode=Automatic waitingChargeComplete=true "
                    "waitingAutoAfterEquip=true castStopsToSkip={}",
                    handStr, _cast.castStopsToSkip);

                break;
            case Press:
                hm.pressActive = true;
                if (hm.wantAutoAttack) {
                    hm.autoActive = true;
                    hm.pressAutocast = true;
                    hm.waitingChargeComplete = true;
                    hm.waitingAutoAfterEquip = true;
                    hm.waitingEnableBumperSecs = 0.f;
                    hm.waitingBeginCast = true;
                    hm.beginCastWaitSecs = 0.f;
                    hm.beginCastRetries = 0;
                    _session.attackEnabled = false;
                    _cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown ? 2 : 1) : 0;
                }

                MAGIC_DEBUG_LOG(
                    "[State] EnterHand: hand={} mode=Press wantAutoAttack={} pressAutocast={} castStopsToSkip={}",
                    handStr, hm.wantAutoAttack, hm.pressAutocast, _cast.castStopsToSkip);

                break;
        }
    }

    bool MagicState::PrepareSlotEntry(int slot, SlotEntry& out) {
        out = {};
        auto* player = GetPlayer();
        if (!player || !Slots::IsValidSlot(slot)) return false;
        out.player = player;

        if (Slots::IsShoutSlot(slot)) {
            out.isShout = true;
            out.shoutID = Slots::GetSlotShout(slot);
            out.shoutForm = out.shoutID ? RE::TESForm::LookupByID(out.shoutID) : nullptr;
            if (!out.shoutForm) return false;
            out.shoutSettings = SpellSettingsDB::Get().GetOrCreate(out.shoutID, out.shoutForm);

            EnsureActiveWithSnapshot(player, slot, false);
            _shout.modeShoutID = out.shoutID;
            _shout.finished = false;
            _shout.isPower = (out.shoutForm->As<RE::SpellItem>() != nullptr);
            _shout.powerAutoSecs = 0.f;
            _left = {};
            _left.finished = true;
            _right = {};
            _right.finished = true;
            _session.modeSpellLeft = nullptr;
            _session.modeSpellRight = nullptr;

            MAGIC_DEBUG_LOG("[State] PrepareSlotEntry: shout slot={} shoutID={:#010x} isPower={} mode={}", slot,
                            out.shoutID, _shout.isPower, static_cast<int>(std::to_underlying(out.shoutSettings.mode)));

            return true;
        }

        using enum Domain::Hand;
        out.rightID = Slots::GetSlotSpell(slot, Right);
        out.leftID = Slots::GetSlotSpell(slot, Left);
        out.rightSpell = out.rightID ? RE::TESForm::LookupByID<RE::SpellItem>(out.rightID) : nullptr;
        out.leftSpell = out.leftID ? RE::TESForm::LookupByID<RE::SpellItem>(out.leftID) : nullptr;
        out.hasRight = (out.rightSpell != nullptr);
        out.hasLeft = (out.leftSpell != nullptr);
        if (!out.hasRight && !out.hasLeft) return false;

        if (out.hasRight) out.rightSettings = SpellSettingsDB::Get().GetOrCreate(out.rightID, out.rightSpell);
        if (out.hasLeft) out.leftSettings = SpellSettingsDB::Get().GetOrCreate(out.leftID, out.leftSpell);

        EnsureActiveWithSnapshot(player, slot);
        _session.modeSpellRight = out.rightSpell;
        _session.modeSpellLeft = out.leftSpell;

        if (out.hasRight) {
            _right.mode = out.rightSettings.mode;
            _right.wantAutoAttack = out.rightSettings.autoAttack;
        } else {
            _right = {};
        }
        if (out.hasLeft) {
            _left.mode = out.leftSettings.mode;
            _left.wantAutoAttack = out.leftSettings.autoAttack;
        } else {
            _left = {};
        }
        return true;
    }

    void MagicState::StartShoutPress() {
        MAGIC_DEBUG_LOG("[State] StartShoutPress: held={} modeShoutID={:#010x}", _shout.held, _shout.modeShoutID);

        _shout.held = true;
        _shout.heldSecs = 0.f;
        if (_outbound.dispatchShout) _outbound.dispatchShout(1.0f, 0.0f);
    }

    void MagicState::StopShoutPress() {
        MAGIC_DEBUG_LOG("[State] StopShoutPress: held={} heldSecs={:.3f} modeShoutID={:#010x}", _shout.held,
                        _shout.heldSecs, _shout.modeShoutID);

        if (!_shout.held) return;
        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
        if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
        _shout.held = false;
        _shout.heldSecs = 0.f;
    }

    SlotPressResult MagicState::OnSlotPressed(int slot) {
        MAGIC_DEBUG_LOG("[State] OnSlotPressed: slot={} active={} activeSlot={} modeShoutID={:#010x}", slot,
                        _session.active, _session.activeSlot, _shout.modeShoutID);

        using enum Domain::Hand;
        using enum ActivationMode;

        if (Slots::IsShoutSlot(slot)) {
            if (_session.active && slot == _session.activeSlot && _shout.modeShoutID != 0) {
                if (_shout.finished) return SlotPressResult::None;
                const auto ss = SpellSettingsDB::Get().Get(_shout.modeShoutID);
                if (ss && ss->mode == Press) {
                    MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout Press toggle -> StopShoutPress + finish");

                    StopShoutPress();
                    _shout.finished = true;
                    TryFinalizeExit();
                }
                return SlotPressResult::None;
            }
            if (_session.active && slot != _session.activeSlot) {
                if (!CanOverwriteNow()) return SlotPressResult::None;
                _session.firstInterrupt = 0;
                PrepareForOverwriteToSlot(slot);
            }
            SlotEntry e{};
            if (!PrepareSlotEntry(slot, e)) return SlotPressResult::None;
            if ((e.shoutSettings.mode == Hold || e.shoutSettings.mode == Automatic) && !_shout.isPower &&
                e.player->GetVoiceRecoveryTime() > 0.f) {
                MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout on cooldown -> early exit");

                _shout.finished = true;
                TryFinalizeExit();
                return SlotPressResult::None;
            }

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: EquipShoutInVoice shoutID={:#010x} isPower={} mode={}", e.shoutID,
                            _shout.isPower, static_cast<int>(std::to_underlying(e.shoutSettings.mode)));

            if (_outbound.equipShoutInVoice) _outbound.equipShoutInVoice(e.shoutForm);
            _restore.dirtyShout = true;

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: calling StartShoutPress (mode={})",
                            static_cast<int>(std::to_underlying(e.shoutSettings.mode)));

            StartShoutPress();
            if (e.shoutSettings.mode == Automatic) _shout.powerAutoSecs = 0.f;
            return SlotPressResult::None;
        }

        if (_session.active && slot == _session.activeSlot) {
            const bool needL = (_session.modeSpellLeft != nullptr);
            const bool needR = (_session.modeSpellRight != nullptr);
            const bool pressL = needL && _left.mode == Press && _left.pressActive;
            const bool pressR = needR && _right.mode == Press && _right.pressActive;
            if (!pressL && !pressR) return SlotPressResult::None;

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: active slot pressed again, toggling press -> pressL={} pressR={}",
                            pressL, pressR);

            if (pressL && pressR) {
                FinishHand(Left);
                FinishHand(Right);
                ExitAllNow();
                return SlotPressResult::Deactivated;
            }
            if (pressL) FinishHand(Left);
            if (pressR) FinishHand(Right);
            TryFinalizeExit();
            return SlotPressResult::Deactivated;
        }

        if (_session.active && slot != _session.activeSlot) {
            if (!CanOverwriteNow()) return SlotPressResult::None;
            _session.firstInterrupt = 0;
            PrepareForOverwriteToSlot(slot);
        }

        if (const auto afford = ComputeSlotAffordability(slot); afford.hasSpells && !afford.canCast) {
            ExitAllNow();
            return SlotPressResult::None;
        }

        SlotEntry e{};
        if (!PrepareSlotEntry(slot, e)) return SlotPressResult::None;

        _session.isDualCasting = false;
        if (e.hasRight && e.hasLeft && e.rightSettings.mode == Automatic && e.leftSettings.mode == Automatic &&
            e.rightID == e.leftID && GetDualCastCostMultiplier(e.player, e.rightSpell) > 2.f) {
            _session.isDualCasting = true;
        }

        if (!e.hasRight) {
            DisableHand(Right);
            SetModeSpellsFromHand(Right, nullptr);
        }
        if (!e.hasLeft) {
            DisableHand(Left);
            SetModeSpellsFromHand(Left, nullptr);
        }
        if (!e.hasLeft && !e.hasRight) {
            ExitAllNow();
            return SlotPressResult::None;
        }

        auto* player = e.player;
        _inSlotSetup = true;
        UpdatePrevExtraEquippedForOverlay([this, player, &e] {
            if (e.hasRight) {
                if (_outbound.equipSpellInHand) _outbound.equipSpellInHand(e.rightSpell, Right);
                MarkDirty(Right);
            }
            if (e.hasLeft) {
                if (_outbound.equipSpellInHand) _outbound.equipSpellInHand(e.leftSpell, Left);
                MarkDirty(Left);
                if (!e.hasRight && SpellClassify::IsTwoHandedSpell(e.leftSpell)) {
                    MarkDirty(Right);
                }
            }
        });
        _inSlotSetup = false;

        if (e.hasRight) {
            SetModeSpellsFromHand(Right, e.rightSpell);
            EnterHand(Right, e.rightSettings);
        } else {
            _right = {};
        }
        if (e.hasLeft) {
            SetModeSpellsFromHand(Left, e.leftSpell);
            EnterHand(Left, e.leftSettings);
        } else {
            _left = {};
        }

        return SlotPressResult::None;
    }

    void MagicState::OnSlotReleased(int slot) {
        MAGIC_DEBUG_LOG(
            "[State] OnSlotReleased: slot={} active={} activeSlot={} modeShoutID={:#010x} isPower={} held={}", slot,
            _session.active, _session.activeSlot, _shout.modeShoutID, _shout.isPower, _shout.held);

        if (!_session.active || slot != _session.activeSlot) return;

        if (_shout.modeShoutID != 0) {
            const auto ss = SpellSettingsDB::Get().Get(_shout.modeShoutID);
            const auto mode = ss ? ss->mode : ActivationMode::Hold;

            MAGIC_DEBUG_LOG("[State] OnSlotReleased: shout path mode={}", static_cast<int>(std::to_underlying(mode)));

            if (mode == ActivationMode::Hold) {
                StopShoutPress();
                if (_shout.isPower) {
                    MAGIC_DEBUG_LOG("[State] OnSlotReleased: power Hold release -> finishing + TryFinalizeExit");

                    _shout.finished = true;
                    TryFinalizeExit();
                } else {
                    MAGIC_DEBUG_LOG("[State] OnSlotReleased: shout Hold release -> waitingStopEvent");

                    _shout.waitingStopEvent = true;
                }
            }
            return;
        }

        using enum Domain::Hand;
        auto handleHoldRelease = [&](Domain::Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.holdActive) return;
            hm.holdActive = false;
            const auto id = Slots::GetSlotSpell(_session.activeSlot, hand);
            const auto* spell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;
            if (!spell || spell->GetChargeTime() <= 0.f) {
                FinishHand(hand);
                return;
            }
            const auto src =
                IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
            if (auto const* caster = GetMagicCaster(GetPlayer(), src); !IsChargeComplete(caster, spell)) {
                FinishHand(hand);
                return;
            }
            if (!hm.wantAutoAttack) {
                FinishHand(hand);
                return;
            }
            StopAutoAttack(hand);
            hm.holdFiredAndWaitingCastStop = true;
        };

        handleHoldRelease(Left);
        handleHoldRelease(Right);
        TryFinalizeExit();
    }
}