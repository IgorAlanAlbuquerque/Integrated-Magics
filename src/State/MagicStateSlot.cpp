#include <utility>

#include "Action.h"
#include "Config/Slots.h"
#include "InventoryUtil.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "State.h"

namespace IntegratedMagic {

    void MagicState::SetModeSpellsFromHand(Slots::Hand hand, RE::SpellItem* spell) {
        if (IsLeft(hand))
            _session.modeSpellLeft = spell;
        else
            _session.modeSpellRight = spell;
    }

    void MagicState::DisableHand(Slots::Hand hand) {
#ifdef DEBUG
        spdlog::info("[State] DisableHand: hand={}", IsLeft(hand) ? "Left" : "Right");
#endif
        StopAutoAttack(hand);
        ModeFor(hand) = {};
        ModeFor(hand).finished = true;
        SetModeSpellsFromHand(hand, nullptr);
    }

    void MagicState::FinishHand(Slots::Hand hand) {
#ifdef DEBUG
        spdlog::info("[State] FinishHand: hand={}", IsLeft(hand) ? "Left" : "Right");
#endif
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

    void MagicState::TogglePressHand(Slots::Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;
        hm.pressActive = !hm.pressActive;
        if (!hm.pressActive) FinishHand(hand);
    }

    void MagicState::EnterHand(Slots::Hand hand, const SpellSettings& ss) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
        hm = {};
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif
        switch (ss.mode) {
            case Hold:
                hm.holdActive = true;
                if (hm.wantAutoAttack) {
                    hm.waitingAutoAfterEquip = true;
                    hm.waitingEnableBumperSecs = 0.f;
                    hm.waitingBeginCast = false;
                    hm.beginCastWaitSecs = 0.f;
                    hm.beginCastRetries = 0;
                    _session.attackEnabled = false;
                    _cast.pendingSkipFirstCastStop = true;
                }
#ifdef DEBUG
                spdlog::info(
                    "[State] EnterHand: hand={} mode=Hold wantAutoAttack={} "
                    "waitingAutoAfterEquip={} pendingSkipFirstCastStop={}",
                    handStr, hm.wantAutoAttack, hm.waitingAutoAfterEquip, _cast.pendingSkipFirstCastStop);
#endif
                break;

            case Automatic:
                hm.autoActive = true;
                hm.waitingChargeComplete = true;
                hm.waitingAutoAfterEquip = true;
                hm.wantAutoAttack = true;
                hm.waitingEnableBumperSecs = 0.f;
                hm.waitingBeginCast = false;
                hm.beginCastWaitSecs = 0.f;
                hm.beginCastRetries = 0;
                _session.attackEnabled = false;
                _cast.pendingSkipFirstCastStop = true;
#ifdef DEBUG
                spdlog::info(
                    "[State] EnterHand: hand={} mode=Automatic waitingChargeComplete=true "
                    "waitingAutoAfterEquip=true pendingSkipFirstCastStop={}",
                    handStr, _cast.pendingSkipFirstCastStop);
#endif
                break;

            case Press:
                hm.pressActive = true;
#ifdef DEBUG
                spdlog::info("[State] EnterHand: hand={} mode=Press", handStr);
#endif
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
            out.shoutSettings = SpellSettingsDB::Get().GetOrCreate(out.shoutID);

            EnsureActiveWithSnapshot(player, slot);
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
#ifdef DEBUG
            spdlog::info("[State] PrepareSlotEntry: shout slot={} shoutID={:#010x} isPower={} mode={}", slot,
                         out.shoutID, _shout.isPower, static_cast<int>(std::to_underlying(out.shoutSettings.mode)));
#endif
            return true;
        }

        using enum Slots::Hand;
        out.rightID = Slots::GetSlotSpell(slot, Right);
        out.leftID = Slots::GetSlotSpell(slot, Left);
        out.rightSpell = out.rightID ? RE::TESForm::LookupByID<RE::SpellItem>(out.rightID) : nullptr;
        out.leftSpell = out.leftID ? RE::TESForm::LookupByID<RE::SpellItem>(out.leftID) : nullptr;
        out.hasRight = (out.rightSpell != nullptr);
        out.hasLeft = (out.leftSpell != nullptr);
        if (!out.hasRight && !out.hasLeft) return false;

        if (out.hasRight) out.rightSettings = SpellSettingsDB::Get().GetOrCreate(out.rightID);
        if (out.hasLeft) out.leftSettings = SpellSettingsDB::Get().GetOrCreate(out.leftID);

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
#ifdef DEBUG
        spdlog::info("[State] StartShoutPress: held={} modeShoutID={:#010x}", _shout.held, _shout.modeShoutID);
#endif
        _shout.held = true;
        _shout.heldSecs = 0.f;
        detail::DispatchShout(1.0f, 0.0f);
    }

    void MagicState::StopShoutPress() {
#ifdef DEBUG
        spdlog::info("[State] StopShoutPress: held={} heldSecs={:.3f} modeShoutID={:#010x}", _shout.held,
                     _shout.heldSecs, _shout.modeShoutID);
#endif
        if (!_shout.held) return;
        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
        detail::DispatchShout(0.0f, held);
        _shout.held = false;
        _shout.heldSecs = 0.f;
    }

    void MagicState::OnSlotPressed(int slot) {
#ifdef DEBUG
        spdlog::info("[State] OnSlotPressed: slot={} active={} activeSlot={} modeShoutID={:#010x}", slot,
                     _session.active, _session.activeSlot, _shout.modeShoutID);
#endif
        using enum Slots::Hand;
        using enum ActivationMode;

        if (Slots::IsShoutSlot(slot)) {
            if (_session.active && slot == _session.activeSlot && _shout.modeShoutID != 0) {
                if (_shout.finished) return;
                if (SpellSettingsDB::Get().GetOrCreate(_shout.modeShoutID).mode == Press) {
#ifdef DEBUG
                    spdlog::info("[State] OnSlotPressed: shout Press toggle -> StopShoutPress + finish");
#endif
                    StopShoutPress();
                    _shout.finished = true;
                    TryFinalizeExit();
                }
                return;
            }
            if (_session.active && slot != _session.activeSlot) {
                if (!CanOverwriteNow()) return;
                _session.firstInterrupt = 0;
                PrepareForOverwriteToSlot(slot);
            }
            SlotEntry e{};
            if (!PrepareSlotEntry(slot, e)) return;
            if ((e.shoutSettings.mode == Hold || e.shoutSettings.mode == Automatic) && !_shout.isPower &&
                e.player->GetVoiceRecoveryTime() > 0.f) {
#ifdef DEBUG
                spdlog::info("[State] OnSlotPressed: shout on cooldown -> early exit");
#endif
                _shout.finished = true;
                TryFinalizeExit();
                return;
            }
#ifdef DEBUG
            spdlog::info("[State] OnSlotPressed: EquipShoutInVoice shoutID={:#010x} isPower={} mode={}", e.shoutID,
                         _shout.isPower, static_cast<int>(std::to_underlying(e.shoutSettings.mode)));
#endif
            MagicAction::EquipShoutInVoice(e.player, e.shoutForm);
            _restore.dirtyShout = true;
#ifdef DEBUG
            spdlog::info("[State] OnSlotPressed: calling StartShoutPress (mode={})",
                         static_cast<int>(std::to_underlying(e.shoutSettings.mode)));
#endif
            StartShoutPress();
            if (e.shoutSettings.mode == Automatic) _shout.powerAutoSecs = 0.f;
            return;
        }

        if (_session.active && slot == _session.activeSlot) {
            const bool needL = (_session.modeSpellLeft != nullptr);
            const bool needR = (_session.modeSpellRight != nullptr);
            const bool pressL = needL && _left.mode == Press && _left.pressActive;
            const bool pressR = needR && _right.mode == Press && _right.pressActive;
            if (!pressL && !pressR) return;
            if (pressL && pressR) {
                FinishHand(Left);
                FinishHand(Right);
                ExitAllNow();
                return;
            }
            if (pressL) FinishHand(Left);
            if (pressR) FinishHand(Right);
            TryFinalizeExit();
            return;
        }

        if (_session.active && slot != _session.activeSlot) {
            if (!CanOverwriteNow()) return;
            _session.firstInterrupt = 0;
            PrepareForOverwriteToSlot(slot);
        }

        SlotEntry e{};
        if (!PrepareSlotEntry(slot, e)) return;

        if (e.hasRight && !HasEnoughMagickaForSpell(e.player, e.rightSpell)) {
            e.hasRight = false;
            DisableHand(Right);
        }
        if (e.hasLeft) {
            float available = GetPlayerMagicka(e.player);
            if (e.hasRight) {
                const float rightCost = GetSpellMagickaCost(e.player, e.rightSpell);
                if (e.rightID == e.leftID) {
                    const float mult = GetDualCastCostMultiplier(e.player, e.rightSpell);
                    const float totalCost = (mult > 2.f) ? rightCost * mult : rightCost * 2.f;
                    if (totalCost > 0.f && (available + 1e-2f) < totalCost) {
                        e.hasLeft = false;
                        DisableHand(Left);
                    }
                } else {
                    available -= rightCost;
                    const float leftCost = GetSpellMagickaCost(e.player, e.leftSpell);
                    if (leftCost > 0.f && (available + 1e-2f) < leftCost) {
                        e.hasLeft = false;
                        DisableHand(Left);
                    }
                }
            } else {
                const float leftCost = GetSpellMagickaCost(e.player, e.leftSpell);
                if (leftCost > 0.f && (GetPlayerMagicka(e.player) + 1e-2f) < leftCost) {
                    e.hasLeft = false;
                    DisableHand(Left);
                }
            }
        }

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
            return;
        }

        auto* player = e.player;
        UpdatePrevExtraEquippedForOverlay([this, player, &e] {
            if (e.hasRight) {
                MagicAction::EquipSpellInHand(player, e.rightSpell, Right);
                MarkDirty(Right);
            }
            if (e.hasLeft) {
                MagicAction::EquipSpellInHand(player, e.leftSpell, Left);
                MarkDirty(Left);
            }
        });

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
    }

    void MagicState::OnSlotReleased(int slot) {
#ifdef DEBUG
        spdlog::info("[State] OnSlotReleased: slot={} active={} activeSlot={} modeShoutID={:#010x} isPower={} held={}",
                     slot, _session.active, _session.activeSlot, _shout.modeShoutID, _shout.isPower, _shout.held);
#endif
        if (!_session.active || slot != _session.activeSlot) return;

        if (_shout.modeShoutID != 0) {
            const auto mode = SpellSettingsDB::Get().GetOrCreate(_shout.modeShoutID).mode;
#ifdef DEBUG
            spdlog::info("[State] OnSlotReleased: shout path mode={}", static_cast<int>(std::to_underlying(mode)));
#endif
            if (mode == ActivationMode::Hold) {
                StopShoutPress();
                if (_shout.isPower) {
#ifdef DEBUG
                    spdlog::info("[State] OnSlotReleased: power Hold release -> finishing + TryFinalizeExit");
#endif
                    _shout.finished = true;
                    TryFinalizeExit();
                } else {
#ifdef DEBUG
                    spdlog::info("[State] OnSlotReleased: shout Hold release -> waitingStopEvent");
#endif
                    _shout.waitingStopEvent = true;
                }
            }
            return;
        }

        using enum Slots::Hand;
        auto handleHoldRelease = [&](Slots::Hand hand) {
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
            if (auto const* caster = MagicAction::GetCaster(GetPlayer(), src); !IsChargeComplete(caster, spell)) {
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