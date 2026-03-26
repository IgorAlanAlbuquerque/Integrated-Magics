#include <utility>

#include "Action.h"
#include "Config/EquipSlots.h"
#include "InventoryUtil.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "State.h"

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

        void ClearHandSpellIfNoSnapshot(RE::PlayerCharacter* player, RE::SpellItem const* snapSpell,
                                        RE::SpellItem* modeSpell, Slots::Hand hand) {
            if (snapSpell) return;
            if (modeSpell)
                MagicAction::ClearHandSpell(player, modeSpell, hand);
            else
                MagicAction::ClearHandSpell(player, hand);
        }

        void EquipSpellIfPresent(RE::PlayerCharacter* player, RE::SpellItem* spell, Slots::Hand hand) {
            if (spell) MagicAction::EquipSpellInHand(player, spell, hand);
        }
    }

    MagicState& MagicState::Get() {
        static MagicState inst;
        return inst;
    }

    void MagicState::EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot, bool raiseHandsIfSheathed) {
        if (_session.active) {
#ifdef DEBUG
            spdlog::info("[State] EnsureActiveWithSnapshot: already active, updating slot {} -> {}",
                         _session.activeSlot, slot);
#endif
            _session.activeSlot = slot;
            return;
        }

        CaptureSnapshot(player);
        _restore.prevExtraEquipped.clear();
        _restore.ClearPending();

        auto* pc = const_cast<RE::PlayerCharacter*>(player);
        const auto ws = pc->AsActorState()->GetWeaponState();
        _session.wasHandsDown = (ws == RE::WEAPON_STATE::kSheathed);
#ifdef DEBUG
        spdlog::info("[State] EnsureActiveWithSnapshot: ACTIVATING slot={} wasHandsDown={} weaponState={}", slot,
                     _session.wasHandsDown, static_cast<int>(std::to_underlying(ws)));
#endif
        if (_session.wasHandsDown && raiseHandsIfSheathed) {
            pc->DrawWeaponMagicHands(true);
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
#ifdef DEBUG
        spdlog::info("[State] CaptureSnapshot: snapShoutID={:#010x} rightSpell={:#010x} leftSpell={:#010x}",
                     _restore.snapshot.snapShoutID,
                     _restore.snapshot.rightSpell ? _restore.snapshot.rightSpell->GetFormID() : 0u,
                     _restore.snapshot.leftSpell ? _restore.snapshot.leftSpell->GetFormID() : 0u);
#endif
    }

    void MagicState::RestoreSnapshot(RE::PlayerCharacter* player) {
        using enum Slots::Hand;
        if (!player || !_restore.snapshot.valid) return;

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) return;

#ifdef DEBUG
        spdlog::info("[State] RestoreSnapshot: dirtyLeft={} dirtyRight={} dirtyShout={} snapShoutID={:#010x}",
                     _restore.dirtyLeft, _restore.dirtyRight, _restore.dirtyShout, _restore.snapshot.snapShoutID);
#endif

        _session.wasHandsDown = false;
        MagicAction::ApplySkipEquipAnimReturn(player);

        const auto idx = BuildInventoryIndex(player);
        const auto* rightSlot = EquipUtil::GetHandEquipSlot(Right);
        const auto* leftSlot = EquipUtil::GetHandEquipSlot(Left);
        auto& snap = _restore.snapshot;

        auto* rightSnapSpell = snap.rightObj.base ? nullptr : AsSpell(snap.rightSpell);
        auto* leftSnapSpell = snap.leftObj.base ? nullptr : AsSpell(snap.leftSpell);

        if (_restore.dirtyRight) {
#ifdef DEBUG
            spdlog::info("[State] RestoreSnapshot: restoring Right hand");
#endif
            ClearHandSpellIfNoSnapshot(player, rightSnapSpell, _session.modeSpellRight, Right);
            RestoreOneHand(player, mgr, idx, false, snap.rightObj, rightSlot);
            EquipSpellIfPresent(player, rightSnapSpell, Right);
        }
        if (_restore.dirtyLeft) {
#ifdef DEBUG
            spdlog::info("[State] RestoreSnapshot: restoring Left hand");
#endif
            ClearHandSpellIfNoSnapshot(player, leftSnapSpell, _session.modeSpellLeft, Left);
            RestoreOneHand(player, mgr, idx, true, snap.leftObj, leftSlot);
            EquipSpellIfPresent(player, leftSnapSpell, Left);
            if (!_restore.dirtyRight && snap.rightObj.base)
                RestoreOneHand(player, mgr, idx, false, snap.rightObj, rightSlot);
        }
        if (_restore.dirtyShout) {
#ifdef DEBUG
            spdlog::info("[State] RestoreSnapshot: restoring shout, snapShoutID={:#010x}", snap.snapShoutID);
#endif
            MagicAction::ClearVoiceShout(player);
            if (snap.snapShoutID) {
                if (auto* form = RE::TESForm::LookupByID(snap.snapShoutID))
                    MagicAction::EquipShoutInVoice(player, form);
            }
        }

        auto idx2 = BuildInventoryIndex(player);
        ReequipPrevExtraEquipped(player, mgr, idx2, _restore.prevExtraEquipped);

        snap.valid = false;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _restore.ClearDirty();
#ifdef DEBUG
        spdlog::info("[State] RestoreSnapshot: done");
#endif
    }

    bool MagicState::HandIsRelevant(Slots::Hand h) const {
        if (_shout.modeShoutID != 0) return false;
        return IsLeft(h) ? (_session.modeSpellLeft != nullptr) : (_session.modeSpellRight != nullptr);
    }

    bool MagicState::AllRelevantHandsFinished() const {
        using enum Slots::Hand;
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
            return SpellSettingsDB::Get().GetOrCreate(_shout.modeShoutID).mode == Press;
        }
        using enum Slots::Hand;
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

        if (!_restore.pendingRestoreAfterSheathe && _shout.modeShoutID == 0 && PlayerIsSheathingOrSheathed(pc))
            return true;

        if (_session.modeSpellRight) {
            auto* caster = MagicAction::GetCaster(pc, RE::MagicSystem::CastingSource::kRightHand);
            if (CasterSpellMismatch(caster, _session.modeSpellRight)) {
#ifdef DEBUG
                spdlog::info("[State] ShouldForceInterrupt: TRUE - Right caster spell mismatch");
#endif
                return true;
            }
        }
        if (_session.modeSpellLeft) {
            auto* caster = MagicAction::GetCaster(pc, RE::MagicSystem::CastingSource::kLeftHand);
            if (CasterSpellMismatch(caster, _session.modeSpellLeft)) {
#ifdef DEBUG
                spdlog::info("[State] ShouldForceInterrupt: TRUE - Left caster spell mismatch");
#endif
                return true;
            }
        }
        return false;
    }

    void MagicState::TryFinalizeExit() {
        if (!_session.active) return;
        const bool allFinished = AllRelevantHandsFinished();
#ifdef DEBUG
        spdlog::info("[State] TryFinalizeExit: allFinished={} left.finished={} right.finished={} shoutFinished={}",
                     allFinished, _left.finished, _right.finished, _shout.finished);
#endif
        if (allFinished) ExitAllNow();
    }

    void MagicState::ExitAllNow() {
#ifdef DEBUG
        spdlog::info(
            "[State] ExitAllNow: modeShoutID={:#010x} shoutIsPower={} shoutFinished={} "
            "firstInterrupt={} active={} wasHandsDown={} pendingRestore={}",
            _shout.modeShoutID, _shout.isPower, _shout.finished, _session.firstInterrupt, _session.active,
            _session.wasHandsDown, _restore.pendingRestore);
#endif

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.finished) {
#ifdef DEBUG
            spdlog::info("[State] ExitAllNow: power path -> pendingPowerRestore, dispatching StopShoutPress");
#endif
            _restore.pendingPowerRestore = true;
            _restore.pendingPowerRestoreDelaySecs = RestoreContext::kPowerRestoreDelaySec;
            StopAllAutoAttack();
            StopShoutPress();
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

        StopAllAutoAttack();
        StopShoutPress();
        CancelAllDelayedStarts();

        if (_session.wasHandsDown && !player->IsInCombat()) {
#ifdef DEBUG
            spdlog::info("[State] ExitAllNow: hands were down -> sheathing before restore");
#endif
            player->DrawWeaponMagicHands(false);
            _restore.pendingRestoreAfterSheathe = true;
            return;
        }

        if (_session.firstInterrupt > 1) {
#ifdef DEBUG
            spdlog::info("[State] ExitAllNow: firstInterrupt={} > 1 -> pendingRestore", _session.firstInterrupt);
#endif
            _restore.pendingRestore = true;
            return;
        }

#ifdef DEBUG
        spdlog::info("[State] ExitAllNow: immediate RestoreSnapshot");
#endif
        RestoreSnapshot(player);
        if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
            auto idx = BuildInventoryIndex(player);
            ReequipPrevExtraEquipped(player, mgr, idx, _restore.prevExtraEquipped);
        }
        ResetSessionState();
    }

    void MagicState::PrepareForOverwriteToSlot(int newSlot) {
#ifdef DEBUG
        spdlog::info("[State] PrepareForOverwriteToSlot: newSlot={}", newSlot);
#endif
        StopAllAutoAttack();
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
#ifdef DEBUG
        spdlog::info("[State] ForceExit: slot={} left.autoActive={} right.autoActive={} aaHeldL={} aaHeldR={}",
                     _session.activeSlot, _left.autoActive, _right.autoActive, _aa.heldLeft, _aa.heldRight);
#endif
        StopAllAutoAttack();
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
#ifdef DEBUG
        spdlog::info("[State] ForceExitNoRestore: discarding snapshot and forcing exit");
#endif
        _restore.snapshot = {};
        ForceExit();
    }
}