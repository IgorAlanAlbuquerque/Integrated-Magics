#include <utility>

#include "Config/ConfigAdapter.h"
#include "Domain/InventoryUtil.h"
#include "Domain/SlotCostUtil.h"
#include "Domain/SpellClassify.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Persistence/SpellSettingsDB.h"
#include "Shared/Hand.h"
#include "Shared/InventoryType.h"

namespace IntegratedMagic {

    void MagicState::SetModeSpellsFromHand(Hand hand, RE::SpellItem* spell) {
        if (IsLeft(hand))
            _session.modeSpellLeft = spell;
        else
            _session.modeSpellRight = spell;
    }

    void MagicState::DisableHand(Hand hand) {
        MAGIC_DEBUG_LOG("[State] DisableHand: hand={}", IsLeft(hand) ? "Left" : "Right");

        if (_aa.Held(hand)) {
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(hand, 0.0f, held);
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }
        ModeFor(hand) = {};
        ModeFor(hand).finished = true;
        SetModeSpellsFromHand(hand, nullptr);
    }

    void MagicState::FinishHand(Hand hand) {
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
        if (_aa.Held(hand)) {
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(hand, 0.0f, held);
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }
        CancelDelayedStart(hand);
    }

    void MagicState::TogglePressHand(Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;
        hm.pressActive = !hm.pressActive;
        if (!hm.pressActive) FinishHand(hand);
    }

    void MagicState::EnterHand(Hand hand, const SpellSettings& ss, bool skipAnim) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
        hm = {};
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;

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

        using enum Hand;
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

    SlotPressAction MagicState::OnSlotPressed(int slot) {
        MAGIC_DEBUG_LOG("[State] OnSlotPressed: slot={} active={} activeSlot={} modeShoutID={:#010x}", slot,
                        _session.active, _session.activeSlot, _shout.modeShoutID);

        using enum Hand;
        using enum ActivationMode;

        // ── Shout path ────────────────────────────────────────────────────────
        if (Slots::IsShoutSlot(slot)) {
            if (_session.active && slot == _session.activeSlot && _shout.modeShoutID != 0) {
                if (_shout.finished) return {};
                const auto ss = SpellSettingsDB::Get().Get(_shout.modeShoutID);
                if (ss && ss->mode == Press) {
                    MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout Press toggle -> StopShoutPress + finish");
                    if (_shout.held) {
                        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                        if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                        _shout.held = false;
                        _shout.heldSecs = 0.f;
                    }
                    _shout.finished = true;
                    TryFinalizeExit();
                }
                return {};
            }
            if (_session.active && slot != _session.activeSlot) {
                if (!CanOverwriteNow()) return {};
                _session.firstInterrupt = 0;
                PrepareForOverwriteToSlot(slot);
            }
            SlotEntry e{};
            if (!PrepareSlotEntry(slot, e)) return {};
            if ((e.shoutSettings.mode == Hold || e.shoutSettings.mode == Automatic) && !_shout.isPower &&
                e.player->GetVoiceRecoveryTime() > 0.f) {
                MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout on cooldown -> early exit");
                _shout.finished = true;
                TryFinalizeExit();
                return {};
            }

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: shoutID={:#010x} isPower={} mode={}", e.shoutID, _shout.isPower,
                            static_cast<int>(std::to_underlying(e.shoutSettings.mode)));

            _restore.dirtyShout = true;
            _shout.held = true;
            _shout.heldSecs = 0.f;
            if (e.shoutSettings.mode == Automatic) _shout.powerAutoSecs = 0.f;

            SlotPressAction action{};
            action.shoutToEquip = e.shoutForm;
            action.startShoutDispatch = true;
            return action;
        }

        // ── Press toggle ──────────────────────────────────────────────────────
        if (_session.active && slot == _session.activeSlot) {
            const bool needL = (_session.modeSpellLeft != nullptr);
            const bool needR = (_session.modeSpellRight != nullptr);
            const bool pressL = needL && _left.mode == Press && _left.pressActive;
            const bool pressR = needR && _right.mode == Press && _right.pressActive;
            if (!pressL && !pressR) return {};

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: active slot pressed again -> pressL={} pressR={}", pressL, pressR);

            if (pressL && pressR) {
                FinishHand(Left);
                FinishHand(Right);
                ExitAllNow();
                return {SlotPressResult::Deactivated};
            }
            if (pressL) FinishHand(Left);
            if (pressR) FinishHand(Right);
            TryFinalizeExit();
            return {SlotPressResult::Deactivated};
        }

        // ── Overwrite ─────────────────────────────────────────────────────────
        if (_session.active && slot != _session.activeSlot) {
            if (!CanOverwriteNow()) return {};
            _session.firstInterrupt = 0;
            PrepareForOverwriteToSlot(slot);
        }

        // ── Affordability ─────────────────────────────────────────────────────
        if (const auto afford = ComputeSlotAffordability(slot); afford.hasSpells && !afford.canCast) {
            if (_session.active) ExitAllNow();
            return {};
        }

        // ── Spell path ────────────────────────────────────────────────────────
        SlotEntry e{};
        if (!PrepareSlotEntry(slot, e)) return {};

        _session.isDualCasting = false;
        if (e.hasRight && e.hasLeft && e.rightSettings.mode == Automatic && e.leftSettings.mode == Automatic &&
            e.rightID == e.leftID && GetDualCastCostMultiplier(e.player, e.rightSpell) > 2.f)
            _session.isDualCasting = true;

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
            return {};
        }

        SlotPressAction action{};
        action.inventorySnapshotBefore = BuildInventoryIndex(e.player);

        if (e.hasRight) {
            action.spellsToEquip.push_back({e.rightSpell, Right});
            MarkDirty(Right);
        }
        if (e.hasLeft) {
            action.spellsToEquip.push_back({e.leftSpell, Left});
            MarkDirty(Left);
            if (!e.hasRight && SpellClassify::IsTwoHandedSpell(e.leftSpell)) MarkDirty(Right);
        }

        _inSlotSetup = true;
        action.skipAnim = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimation();
        if (e.hasRight) {
            SetModeSpellsFromHand(Right, e.rightSpell);
            EnterHand(Right, e.rightSettings, action.skipAnim);
        } else {
            _right = {};
        }
        if (e.hasLeft) {
            SetModeSpellsFromHand(Left, e.leftSpell);
            EnterHand(Left, e.leftSettings, action.skipAnim);
        } else {
            _left = {};
        }

        return action;
    }

    void MagicState::OnEquipComplete(const InventoryIndex& snapshotBefore) {
        auto* player = GetPlayer();
        if (!player) {
            _inSlotSetup = false;
            return;
        }

        const auto after = BuildInventoryIndex(player);
        for (auto* base : snapshotBefore.wornBases) {
            if (after.wornBases.contains(base)) continue;
            const bool exists =
                std::ranges::any_of(_restore.prevExtraEquipped, [&](auto const& e) { return e.base == base; });
            if (!exists) _restore.prevExtraEquipped.push_back({base, nullptr});
        }

        _inSlotSetup = false;
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
                MAGIC_DEBUG_LOG("[State] StopShoutPress: held={} heldSecs={:.3f} modeShoutID={:#010x}", _shout.held,
                                _shout.heldSecs, _shout.modeShoutID);

                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }

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

        using enum Hand;
        auto handleHoldRelease = [&](Hand hand) {
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
            if (_aa.Held(hand)) {
                const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

                MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right",
                                held);

                if (_outbound.dispatchAttack) _outbound.dispatchAttack(hand, 0.0f, held);
                _aa.Held(hand) = false;
                _aa.Secs(hand) = 0.f;
            }
            hm.holdFiredAndWaitingCastStop = true;
        };

        handleHoldRelease(Left);
        handleHoldRelease(Right);
        TryFinalizeExit();
    }
}