#include <utility>
#undef GetObject

#include "Config/ConfigAdapter.h"
#include "Domain/CasterUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/InventoryUtil.h"

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
    }

    MagicState& MagicState::Get() {
        static MagicState inst;
        return inst;
    }

    bool MagicState::EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot, bool raiseHandsIfSheathed) {
        if (_session.active) {
            MAGIC_DEBUG_LOG("[State] EnsureActiveWithSnapshot: already active, updating slot {} -> {}",
                            _session.activeSlot, slot);

            _session.activeSlot = slot;
            return false;
        }

        bool setSkipAndDrawnhands = false;

        CaptureSnapshot(player);
        _restore.prevExtraEquipped.clear();
        _restore.ClearPending();

        auto* pc = const_cast<RE::PlayerCharacter*>(player);
        const auto ws = pc->AsActorState()->GetWeaponState();
        _session.wasHandsDown = (ws == RE::WEAPON_STATE::kSheathed);
#ifdef DEBUG
        const bool snapHasSpells = (_restore.snapshot.rightSpell != nullptr || _restore.snapshot.leftSpell != nullptr);
#endif

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
            setSkipAndDrawnhands = true;
        } else {
            MAGIC_DEBUG_LOG(
                "[State] EnsureActiveWithSnapshot: skipping DrawWeaponMagicHands - wasHandsDown={} "
                "raiseHandsIfSheathed={}",
                _session.wasHandsDown, raiseHandsIfSheathed);
        }

        _session.active = true;
        _session.activeSlot = slot;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _left = {};
        _right = {};
        _aa.Reset();
        _shout.Reset();
        _session.activeTimeoutSecs = 0.f;
        return setSkipAndDrawnhands;
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

    RestoreSnapshotPlan MagicState::BuildRestoreSnapshotPlan(RE::PlayerCharacter* player) {
        using enum Hand;

        RestoreSnapshotPlan plan{};

        if (!player || !_restore.snapshot.valid) return plan;

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) return plan;

        auto& snap = _restore.snapshot;

        MAGIC_DEBUG_LOG(
            "[State] BuildRestoreSnapshotPlan: dirtyLeft={} dirtyRight={} dirtyShout={} snapShoutID={:#010x}",
            _restore.dirtyLeft, _restore.dirtyRight, _restore.dirtyShout, snap.snapShoutID);

        plan.valid = true;
        plan.applySkipEquipAnimReturn = true;
        plan.skipEquipAnimReturn = Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
        plan.inventoryIndex = BuildInventoryIndex(player);
        plan.prevExtraEquipped = _restore.prevExtraEquipped;

        auto* rightSnapSpell = snap.rightObj.base ? nullptr : AsSpell(snap.rightSpell);
        auto* leftSnapSpell = snap.leftObj.base ? nullptr : AsSpell(snap.leftSpell);

        if (_restore.dirtyRight) {
            plan.restoreRightHand = true;
            plan.rightObj = snap.rightObj;

            if (!snap.rightObj.base) {
                if (!rightSnapSpell) {
                    if (_session.modeSpellRight) {
                        plan.clearRightHandByRef = _session.modeSpellRight;
                    } else {
                        plan.clearRightHand = true;
                    }
                }
            }

            plan.equipRightSpell = rightSnapSpell;
        }

        if (_restore.dirtyLeft) {
            plan.restoreLeftHand = true;
            plan.leftObj = snap.leftObj;

            if (!snap.leftObj.base) {
                if (!leftSnapSpell) {
                    if (_session.modeSpellLeft) {
                        plan.clearLeftHandByRef = _session.modeSpellLeft;
                    } else {
                        plan.clearLeftHand = true;
                    }
                }
            }

            plan.equipLeftSpell = leftSnapSpell;

            if (!_restore.dirtyRight && snap.rightObj.base) {
                plan.restoreRightAfterLeftOnly = true;
                plan.rightObj = snap.rightObj;
            }
        }

        if (_restore.dirtyShout) {
            plan.clearVoiceShout = true;

            if (snap.snapShoutID) {
                plan.equipVoiceForm = RE::TESForm::LookupByID(snap.snapShoutID);
            }
        }

        return plan;
    }

    void MagicState::FinalizeRestoreSnapshotPlan(bool resetShout) {
        MAGIC_DEBUG_LOG("[State] FinalizeImmediateExitAfterController");

        _restore.snapshot = {};
        _restore.prevExtraEquipped.clear();
        _restore.ClearDirty();
        _restore.ClearPending();

        if (resetShout) {
            _shout.Reset();
        }

        ResetSessionState();
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
            const auto settings = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
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
#ifdef DEBUG
        const auto ws = pc->AsActorState()->GetWeaponState();
#endif
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

    void MagicState::FinalizeExitAfterController() {
        _restore.snapshot.valid = false;
        _restore.prevExtraEquipped.clear();
        _restore.ClearDirty();

        ResetSessionState();
    }

    StateExitResult MagicState::TryFinalizeExit() {
        if (!_session.active) return {};
        const bool allFinished = AllRelevantHandsFinished();

        MAGIC_DEBUG_LOG("[State] TryFinalizeExit: allFinished={} left.finished={} right.finished={} shoutFinished={}",
                        allFinished, _left.finished, _right.finished, _shout.finished);

        if (allFinished) {
            return ExitAllNow();
        }
        return {};
    }

    StateExitResult MagicState::ExitAllNow() {
        StateExitResult result{};

        using enum Hand;

        auto stopAttack = [&](Hand hand) -> std::optional<StopDispatchIntent> {
            if (!_aa.Held(hand)) return std::nullopt;
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            return StopDispatchIntent{held};
        };

        auto stopShout = [&]() -> std::optional<StopDispatchIntent> {
            if (!_shout.held) return std::nullopt;
            const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
            _shout.held = false;
            _shout.heldSecs = 0.f;
            return StopDispatchIntent{held};
        };

        result.leftAttack = stopAttack(Left);
        result.rightAttack = stopAttack(Right);
        result.shout = stopShout();

        if (_shout.modeShoutID != 0 && _shout.isPower && _shout.finished) {
            _restore.pendingPowerRestore = true;
            _restore.pendingPowerRestoreDelaySecs = RestoreContext::kPowerRestoreDelaySec;

            _shout.modeShoutID = 0;
            _shout.finished = false;
            _shout.held = false;

            _session.active = false;
            _session.activeSlot = -1;
            _left = {};
            _right = {};

            _restore.dirtyShout = true;
            result.waitForPowerRestore = true;
            return result;
        }

        auto* player = GetPlayer();
        if (!player) {
            FinalizeExitAfterController();
            return result;
        }

        if (_session.wasHandsDown && !player->IsInCombat()) {
            player->DrawWeaponMagicHands(false);
            _restore.pendingRestoreAfterSheathe = true;
            result.waitForSheatheRestore = true;
            return result;
        }

        result.restorePlan = BuildRestoreSnapshotPlan(player);
        result.finalizeAfterController = true;
        return result;
    }

    PrepareOverwriteResult MagicState::PrepareForOverwriteToSlot(int newSlot) {
        MAGIC_DEBUG_LOG("[State] PrepareForOverwriteToSlot: newSlot={}", newSlot);

        PrepareOverwriteResult result{};

        using enum Hand;
        if (_aa.Held(Left)) {
            const float held = (_aa.Secs(Left) > 0.f) ? _aa.Secs(Left) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Left) ? "Left" : "Right", held);

            result.leftAttack = StopDispatchIntent{held};
            _aa.Held(Left) = false;
            _aa.Secs(Left) = 0.f;
        }

        if (_aa.Held(Right)) {
            const float held = (_aa.Secs(Right) > 0.f) ? _aa.Secs(Right) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(Right) ? "Left" : "Right", held);

            result.rightAttack = StopDispatchIntent{held};
            _aa.Held(Right) = false;
            _aa.Secs(Right) = 0.f;
        }

        _session.activeSlot = newSlot;
        _session.isDualCasting = false;
        _session.modeSpellLeft = nullptr;
        _session.modeSpellRight = nullptr;
        _left = {};
        _right = {};
        _aa.Reset();
        _shout.Reset();
        _restore.pendingPowerRestore = false;
        _restore.pendingPowerRestoreDelaySecs = 0.f;
        _restore.pendingRestoreAfterSheathe = false;

        return result;
    }

    StateExitResult MagicState::ForceExit() {
        StateExitResult result{};

        if (!_session.active) return result;

        MAGIC_DEBUG_LOG("[State] ForceExit: slot={} left.autoActive={} right.autoActive={} aaHeldL={} aaHeldR={}",
                        _session.activeSlot, _left.autoActive, _right.autoActive, _aa.heldLeft, _aa.heldRight);

        using enum Hand;

        auto stopAttack = [&](Hand hand) -> std::optional<StopDispatchIntent> {
            if (!_aa.Held(hand)) return std::nullopt;

            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
            return StopDispatchIntent{held};
        };

        result.leftAttack = stopAttack(Left);
        result.rightAttack = stopAttack(Right);

        if (auto* pc = GetPlayer(); pc && !pc->IsDead() && _restore.snapshot.valid) {
            auto plan = BuildRestoreSnapshotPlan(pc);
            if (plan.valid) {
                result.restorePlan = std::move(plan);
            }
        }

        result.finalizeAfterController = true;
        result.resetShoutAfterController = true;

        return result;
    }

    StateExitResult MagicState::ForceExitNoRestore() {
        if (!_session.active) return {};

        MAGIC_DEBUG_LOG("[State] ForceExitNoRestore: discarding snapshot and forcing exit");

        _restore.snapshot = {};
        return ForceExit();
    }
}