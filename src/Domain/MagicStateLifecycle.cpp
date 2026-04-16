#include <utility>

#include "Adapters/Outbound/MagicEquip.h"
#include "Domain/InventoryUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "Shared/Hand.h"

namespace IntegratedMagic {
    namespace {
        using WS = RE::WEAPON_STATE;
        using KS = RE::KNOCK_STATE_ENUM;

        bool PlayerIsDead(RE::PlayerCharacter const* pc) { return pc->IsDead(); }
        bool PlayerIsBlocking(RE::PlayerCharacter const* pc) { return pc->IsBlocking(); }

        bool PlayerIsKnockedOrStaggered(RE::PlayerCharacter* pc) {
            auto ks = pc->AsActorState()->GetKnockState();
            return ks != KS::kNormal && ks != KS::kQueued;
        }

        bool PlayerIsSheathingOrSheathed(RE::PlayerCharacter* pc) {
            using enum RE::WEAPON_STATE;
            auto ws = pc->AsActorState()->GetWeaponState();
            return ws == kSheathing || ws == kSheathed || ws == kWantToSheathe;
        }

        inline RE::SpellItem* AsSpell(RE::MagicItem* m) { return m ? m->As<RE::SpellItem>() : nullptr; }

        void ClearHandSpellIfNoSnapshot(RE::SpellItem const* snapSpell, RE::SpellItem* modeSpell, Hand hand,
                                        const Domain::OutboundDelegate& outbound) {
            if (snapSpell) return;
            if (modeSpell) {
                if (outbound.clearHandSpellByRef) outbound.clearHandSpellByRef(modeSpell, hand);
            } else {
                if (outbound.clearHandSpell) outbound.clearHandSpell(hand);
            }
        }

        void EquipSpellIfPresent(RE::SpellItem* spell, Hand hand, const Domain::OutboundDelegate& outbound) {
            if (spell && outbound.equipSpellInHand) outbound.equipSpellInHand(spell, hand);
        }
    }

    MagicState& MagicState::Get() {
        static MagicState inst;
        return inst;
    }

    void MagicState::EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot, bool raiseHandsIfSheathed) {
        if (_session.active) {
            MAGIC_DEBUG_LOG("[State] EnsureActiveWithSnapshot: already active, updating slot {} -> {}",
                            _session.activeSlot, slot);

            _session.activeSlot = slot;
            return;
        }

        CaptureSnapshot(player);
        _restore.prevExtraEquipped.clear();
        _restore.ClearPending();

        auto* pc = const_cast<RE::PlayerCharacter*>(player);
        const auto ws = pc->AsActorState()->GetWeaponState();
        _session.wasHandsDown = (ws == RE::WEAPON_STATE::kSheathed);

        const bool snapHasSpells = (_restore.snapshot.rightSpell != nullptr || _restore.snapshot.leftSpell != nullptr);

        MAGIC_DEBUG_LOG("[State] EnsureActiveWithSnapshot: ACTIVATING slot={} wasHandsDown={} weaponState={}", slot,
                        _session.wasHandsDown, static_cast<int>(std::to_underlying(ws)));

        MAGIC_DEBUG_LOG("[State] EnsureActiveWithSnapshot: snapHasSpells={} (right={:#010x} left={:#010x})",
                        snapHasSpells, _restore.snapshot.rightSpell ? _restore.snapshot.rightSpell->GetFormID() : 0u,
                        _restore.snapshot.leftSpell ? _restore.snapshot.leftSpell->GetFormID() : 0u);

        if (_session.wasHandsDown && raiseHandsIfSheathed) {
            MAGIC_DEBUG_LOG(
                "[State] EnsureActiveWithSnapshot: calling DrawWeaponMagicHands(true) - wasHandsDown=true "
                "snapHasSpells={}",
                snapHasSpells);
            MagicAction::SetSkipEquipVars(pc, true);
            pc->DrawWeaponMagicHands(true);
        } else {
            MAGIC_DEBUG_LOG(
                "[State] EnsureActiveWithSnapshot: skipping DrawWeaponMagicHands - wasHandsDown={} "
                "raiseHandsIfSheathed={}",
                _session.wasHandsDown, raiseHandsIfSheathed);
        }

        _session.active = true;
        _session.activeSlot = slot;
        _session.attackEnabled = false;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _left = {};
        _right = {};
        _aa.Reset();
        _shout.Reset();
        _session.activeTimeoutSecs = 0.f;
    }

    void MagicState::CaptureSnapshot(RE::PlayerCharacter const* player) {
        _restore.snapshot = {};
        if (!player) return;

        auto captureHand = [&](bool leftHand) {
            ObjSnapshot s{};
            if (auto* entry = player->GetEquippedEntryData(leftHand)) {
                if (auto* obj = entry->GetObject()) {
                    if (auto* base = obj->As<RE::TESBoundObject>()) {
                        s.base = base;
                        s.extra = GetWornExtraForHand(entry, leftHand);
                        s.formID = obj->GetFormID();
                    }
                }
            }
            return s;
        };

        auto getSpell = [&](bool leftHand) -> RE::MagicItem* {
            if (player->GetEquippedEntryData(leftHand)) return nullptr;
            auto* f = player->GetEquippedObject(leftHand);
            return f ? f->As<RE::SpellItem>() : nullptr;
        };

        _restore.snapshot.rightObj = captureHand(false);
        _restore.snapshot.leftObj = captureHand(true);
        _restore.snapshot.rightSpell = getSpell(false);
        _restore.snapshot.leftSpell = getSpell(true);

        if (auto* shout = player->GetCurrentShout()) {
            _restore.snapshot.snapShoutID = shout->GetFormID();
        } else {
            auto const& rd = player->GetActorRuntimeData();
            if (auto const* power = rd.selectedPower ? rd.selectedPower->As<RE::SpellItem>() : nullptr) {
                using ST = RE::MagicSystem::SpellType;
                if (power->GetSpellType() == ST::kPower || power->GetSpellType() == ST::kLesserPower)
                    _restore.snapshot.snapShoutID = power->GetFormID();
            }
        }
        _restore.snapshot.valid = true;

        MAGIC_DEBUG_LOG("[State] CaptureSnapshot: snapShoutID={:#010x} rightSpell={:#010x} leftSpell={:#010x}",
                        _restore.snapshot.snapShoutID,
                        _restore.snapshot.rightSpell ? _restore.snapshot.rightSpell->GetFormID() : 0u,
                        _restore.snapshot.leftSpell ? _restore.snapshot.leftSpell->GetFormID() : 0u);
    }

    void MagicState::RestoreSnapshot(RE::PlayerCharacter* player) {
        using enum Hand;
        if (!player || !_restore.snapshot.valid) return;

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) return;

        MAGIC_DEBUG_LOG("[State] RestoreSnapshot: dirtyLeft={} dirtyRight={} dirtyShout={} snapShoutID={:#010x}",
                        _restore.dirtyLeft, _restore.dirtyRight, _restore.dirtyShout, _restore.snapshot.snapShoutID);

        _session.wasHandsDown = false;
        if (_outbound.applySkipEquipAnimReturn) _outbound.applySkipEquipAnimReturn();

        const auto idx = BuildInventoryIndex(player);
        const auto* rightSlot = _outbound.getHandEquipSlot ? _outbound.getHandEquipSlot(Right) : nullptr;
        const auto* leftSlot = _outbound.getHandEquipSlot ? _outbound.getHandEquipSlot(Left) : nullptr;
        auto& snap = _restore.snapshot;

        auto* rightSnapSpell = snap.rightObj.base ? nullptr : AsSpell(snap.rightSpell);
        auto* leftSnapSpell = snap.leftObj.base ? nullptr : AsSpell(snap.leftSpell);

        if (_restore.dirtyRight) {
            MAGIC_DEBUG_LOG("[State] RestoreSnapshot: restoring Right hand");

            if (!snap.rightObj.base)
                ClearHandSpellIfNoSnapshot(rightSnapSpell, _session.modeSpellRight, Right, _outbound);
            if (_outbound.restoreOneHand) _outbound.restoreOneHand(false, idx, snap.rightObj, rightSlot);
            EquipSpellIfPresent(rightSnapSpell, Right, _outbound);
        }
        if (_restore.dirtyLeft) {
            MAGIC_DEBUG_LOG("[State] RestoreSnapshot: restoring Left hand");

            if (!snap.leftObj.base) ClearHandSpellIfNoSnapshot(leftSnapSpell, _session.modeSpellLeft, Left, _outbound);
            if (_outbound.restoreOneHand) _outbound.restoreOneHand(true, idx, snap.leftObj, leftSlot);
            EquipSpellIfPresent(leftSnapSpell, Left, _outbound);
            if (!_restore.dirtyRight && snap.rightObj.base)
                if (_outbound.restoreOneHand) _outbound.restoreOneHand(false, idx, snap.rightObj, rightSlot);
        }

        if (_restore.dirtyShout) {
            MAGIC_DEBUG_LOG("[State] RestoreSnapshot: restoring shout, snapShoutID={:#010x}", snap.snapShoutID);

            if (_outbound.clearVoiceShout) _outbound.clearVoiceShout();
            if (snap.snapShoutID) {
                if (auto* form = RE::TESForm::LookupByID(snap.snapShoutID))
                    if (_outbound.equipShoutInVoice) _outbound.equipShoutInVoice(form);
            }
        }

        if (_outbound.reequipPrevExtraEquipped) _outbound.reequipPrevExtraEquipped(player, _restore.prevExtraEquipped);

        snap.valid = false;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _restore.ClearDirty();

        MAGIC_DEBUG_LOG("[State] RestoreSnapshot: done");
    }

    bool MagicState::HandIsRelevant(Hand h) const {
        if (_shout.modeShoutID != 0) return false;
        return IsLeft(h) ? (_session.modeSpellLeft != nullptr) : (_session.modeSpellRight != nullptr);
    }

    bool MagicState::AllRelevantHandsFinished() const {
        using enum Hand;
        if (_shout.modeShoutID != 0) return _shout.finished;
        const bool needL = HandIsRelevant(Left);
        const bool needR = HandIsRelevant(Right);
        return (!needL || _left.finished) && (!needR || _right.finished);
    }

    bool MagicState::CanOverwriteNow() const {
        using enum IntegratedMagic::ActivationMode;
        if (!_session.active || _session.activeSlot < 0) return false;
        if (_shout.modeShoutID != 0) {
            if (_shout.finished) return false;
            const auto settings = SpellSettingsDB::Get().Get(_shout.modeShoutID);
            return settings && settings->mode == Press;
        }
        using enum Hand;
        const bool needL = (_session.modeSpellLeft != nullptr);
        const bool needR = (_session.modeSpellRight != nullptr);
        if (!needL && !needR) return false;
        if ((needL && (_left.holdActive || _left.autoActive || _left.holdFiredAndWaitingCastStop)) ||
            (needR && (_right.holdActive || _right.autoActive || _right.holdFiredAndWaitingCastStop))) {
            return false;
        }
        int pressCount = 0;
        if (needL && _left.mode == Press) ++pressCount;
        if (needR && _right.mode == Press) ++pressCount;
        if (pressCount == 0) return false;
        if (needL && _left.mode != Press && !_left.finished) return false;
        if (needR && _right.mode != Press && !_right.finished) return false;
        return true;
    }

    bool MagicState::ShouldForceInterrupt() const {
        if (!_session.active) return false;
        auto* pc = GetPlayer();
        if (!pc) return true;
        if (PlayerIsDead(pc)) return true;
        if (PlayerIsKnockedOrStaggered(pc) && (!_left.pressActive && !_right.pressActive)) return true;
        if (PlayerIsBlocking(pc) && (!_left.pressActive && !_right.pressActive)) return true;

        const auto ws = pc->AsActorState()->GetWeaponState();
        if (!_restore.pendingRestoreAfterSheathe && _shout.modeShoutID == 0 && PlayerIsSheathingOrSheathed(pc)) {
            MAGIC_DEBUG_LOG("[State] ShouldForceInterrupt: TRUE - player sheathing/sheathed weaponState={}",
                            static_cast<int>(std::to_underlying(ws)));
            return true;
        }

        if (_session.modeSpellRight) {
            auto* caster = GetMagicCaster(pc, RE::MagicSystem::CastingSource::kRightHand);
            if (CasterSpellMismatch(caster, _session.modeSpellRight)) {
                MAGIC_DEBUG_LOG("[State] ShouldForceInterrupt: TRUE - Right caster spell mismatch");

                return true;
            }
        }
        if (_session.modeSpellLeft) {
            auto* caster = GetMagicCaster(pc, RE::MagicSystem::CastingSource::kLeftHand);
            if (CasterSpellMismatch(caster, _session.modeSpellLeft)) {
                MAGIC_DEBUG_LOG("[State] ShouldForceInterrupt: TRUE - Left caster spell mismatch");

                return true;
            }
        }
        return false;
    }

    void MagicState::TryFinalizeExit() {
        if (!_session.active) return;
        const bool allFinished = AllRelevantHandsFinished();

        MAGIC_DEBUG_LOG("[State] TryFinalizeExit: allFinished={} left.finished={} right.finished={} shoutFinished={}",
                        allFinished, _left.finished, _right.finished, _shout.finished);

        if (allFinished) ExitAllNow();
    }

    void MagicState::ExitAllNow() {
        MAGIC_DEBUG_LOG(
            "[State] ExitAllNow: modeShoutID={:#010x} shoutIsPower={} shoutFinished={} "
            "firstInterrupt={} active={} wasHandsDown={} pendingRestore={}",
            _shout.modeShoutID, _shout.isPower, _shout.finished, _session.firstInterrupt, _session.active,
            _session.wasHandsDown, _restore.pendingRestore);

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.finished) {
            MAGIC_DEBUG_LOG("[State] ExitAllNow: power path -> pendingPowerRestore, dispatching StopShoutPress");

            _restore.pendingPowerRestore = true;
            _restore.pendingPowerRestoreDelaySecs = RestoreContext::kPowerRestoreDelaySec;
            using enum Hand;
            if (_aa.Held(Left)) {
                const float held = (_aa.Secs(Left) > 0.f) ? _aa.Secs(Left) : 0.1f;

                MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Left) ? "Left" : "Right",
                                held);

                if (_outbound.dispatchAttack) _outbound.dispatchAttack(Left, 0.0f, held);
                _aa.Held(Left) = false;
                _aa.Secs(Left) = 0.f;
            }

            if (_aa.Held(Right)) {
                const float held = (_aa.Secs(Right) > 0.f) ? _aa.Secs(Right) : 0.1f;

                MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Right) ? "Left" : "Right",
                                held);

                if (_outbound.dispatchAttack) _outbound.dispatchAttack(Right, 0.0f, held);
                _aa.Held(Right) = false;
                _aa.Secs(Right) = 0.f;
            }
            if (_shout.held) {
                const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
                _shout.held = false;
                _shout.heldSecs = 0.f;
            }
            CancelAllDelayedStarts();
            _session.active = false;
            _session.activeSlot = -1;
            _left = {};
            _right = {};
            _restore.dirtyShout = true;
            _shout.modeShoutID = 0;
            _shout.finished = false;
            _shout.held = false;
            return;
        }

        auto* player = GetPlayer();
        if (!player) {
            ResetSessionState();
            _restore.snapshot.valid = false;
            return;
        }

        using enum Hand;
        if (_aa.Held(Left)) {
            const float held = (_aa.Secs(Left) > 0.f) ? _aa.Secs(Left) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Left) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Left, 0.0f, held);
            _aa.Held(Left) = false;
            _aa.Secs(Left) = 0.f;
        }

        if (_aa.Held(Right)) {
            const float held = (_aa.Secs(Right) > 0.f) ? _aa.Secs(Right) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Right) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Right, 0.0f, held);
            _aa.Held(Right) = false;
            _aa.Secs(Right) = 0.f;
        }
        if (_shout.held) {
            const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
            if (_outbound.dispatchShout) _outbound.dispatchShout(0.0f, held);
            _shout.held = false;
            _shout.heldSecs = 0.f;
        }
        CancelAllDelayedStarts();

        if (_session.wasHandsDown && !player->IsInCombat()) {
            MAGIC_DEBUG_LOG("[State] ExitAllNow: hands were down -> sheathing before restore");

            player->DrawWeaponMagicHands(false);
            _restore.pendingRestoreAfterSheathe = true;
            return;
        }

        if (_session.firstInterrupt > 1) {
            MAGIC_DEBUG_LOG("[State] ExitAllNow: firstInterrupt={} > 1 -> pendingRestore", _session.firstInterrupt);

            _restore.pendingRestore = true;
            return;
        }

        MAGIC_DEBUG_LOG("[State] ExitAllNow: immediate RestoreSnapshot");

        RestoreSnapshot(player);
        if (_outbound.reequipPrevExtraEquipped) {
            _outbound.reequipPrevExtraEquipped(player, _restore.prevExtraEquipped);
        }
        ResetSessionState();
    }

    void MagicState::PrepareForOverwriteToSlot(int newSlot) {
        MAGIC_DEBUG_LOG("[State] PrepareForOverwriteToSlot: newSlot={}", newSlot);

        using enum Hand;
        if (_aa.Held(Left)) {
            const float held = (_aa.Secs(Left) > 0.f) ? _aa.Secs(Left) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Left) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Left, 0.0f, held);
            _aa.Held(Left) = false;
            _aa.Secs(Left) = 0.f;
        }

        if (_aa.Held(Right)) {
            const float held = (_aa.Secs(Right) > 0.f) ? _aa.Secs(Right) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Right) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Right, 0.0f, held);
            _aa.Held(Right) = false;
            _aa.Secs(Right) = 0.f;
        }
        _session.activeSlot = newSlot;
        _session.attackEnabled = false;
        _session.isDualCasting = false;
        _session.dualCastSkipCastStops = 0;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _left = {};
        _right = {};
        _aa.Reset();
        _shout.Reset();
        _restore.pendingPowerRestore = false;
        _restore.pendingPowerRestoreDelaySecs = 0.f;
        _restore.pendingRestoreAfterSheathe = false;
    }

    void MagicState::ForceExit() {
        if (!_session.active) return;

        MAGIC_DEBUG_LOG("[State] ForceExit: slot={} left.autoActive={} right.autoActive={} aaHeldL={} aaHeldR={}",
                        _session.activeSlot, _left.autoActive, _right.autoActive, _aa.heldLeft, _aa.heldRight);

        using enum Hand;
        if (_aa.Held(Left)) {
            const float held = (_aa.Secs(Left) > 0.f) ? _aa.Secs(Left) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Left) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Left, 0.0f, held);
            _aa.Held(Left) = false;
            _aa.Secs(Left) = 0.f;
        }

        if (_aa.Held(Right)) {
            const float held = (_aa.Secs(Right) > 0.f) ? _aa.Secs(Right) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Right) ? "Left" : "Right", held);

            if (_outbound.dispatchAttack) _outbound.dispatchAttack(Right, 0.0f, held);
            _aa.Held(Right) = false;
            _aa.Secs(Right) = 0.f;
        }
        CancelAllDelayedStarts();
        _left = {};
        _right = {};

        if (auto* pc = GetPlayer(); pc && !pc->IsDead() && _restore.snapshot.valid) RestoreSnapshot(pc);

        _restore.snapshot = {};
        _restore.ClearPending();
        _cast.Reset();
        ResetSessionState();
    }

    void MagicState::ForceExitNoRestore() {
        if (!_session.active) return;

        MAGIC_DEBUG_LOG("[State] ForceExitNoRestore: discarding snapshot and forcing exit");

        _restore.snapshot = {};
        ForceExit();
    }

    void MagicState::SetOutboundDelegate(const Domain::OutboundDelegate& delegate) { _outbound = delegate; }
}