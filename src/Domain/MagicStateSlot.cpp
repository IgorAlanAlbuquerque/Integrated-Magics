#include <utility>

#include "Config/ConfigAdapter.h"
#include "Config/Slots.h"
#include "Domain/CasterUtil.h"
#include "Domain/SlotCostUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Shared/AttackEnabledResult.h"
#include "Shared/Hand.h"
#include "Shared/InventoryType.h"
#include "Shared/InventoryUtil.h"
#include "Shared/SpellClassify.h"

namespace IntegratedMagic {

    void MagicState::SetModeSpellsFromHand(Hand hand, RE::SpellItem* spell) {
        if (IsLeft(hand))
            _session.modeSpellLeft = spell;
        else
            _session.modeSpellRight = spell;
    }

    DisableHandResult MagicState::DisableHand(Hand hand) {
        MAGIC_DEBUG_LOG("[State] DisableHand: hand={}", IsLeft(hand) ? "Left" : "Right");

        DisableHandResult result{};

        if (_aa.Held(hand)) {
            const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            result.attack = StopDispatchIntent{held};
            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }

        ModeFor(hand) = {};
        ModeFor(hand).finished = true;
        SetModeSpellsFromHand(hand, nullptr);

        return result;
    }

    float MagicState::FinishHand(Hand hand) {
        MAGIC_DEBUG_LOG("[State] FinishHand: hand={}", IsLeft(hand) ? "Left" : "Right");

        auto& hm = ModeFor(hand);
        hm.finished = true;
        hm.holdActive = false;
        hm.autoActive = false;
        hm.pressActive = false;
        hm.waitingChargeComplete = false;
        hm.holdFiredAndWaitingCastStop = false;
        hm.pendingRestartNextFrame = false;
        hm.startRequestSecs = 0.f;
        hm.castingElapsedSecs = 0.f;
        hm.autoCastPhase = AutoCastPhase::Done;
        float held = -1;
        if (_aa.Held(hand)) {
            held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

            MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right", held);

            _aa.Held(hand) = false;
            _aa.Secs(hand) = 0.f;
        }
        return held;
    }

    void MagicState::TogglePressHand(Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;
        const bool newPressActive = !hm.pressActive;
        MAGIC_DEBUG_LOG("[State] TogglePressHand: hand={} mode={} wantAuto={} pressActive={} → {}",
                        IsLeft(hand) ? "Left" : "Right", static_cast<int>(ss.mode), ss.autoAttack,
                        hm.pressActive, newPressActive);
        hm.pressActive = newPressActive;
        if (!hm.pressActive) FinishHand(hand);
    }

    void MagicState::EnterHand(Hand hand, const SpellSettings& ss, bool /*skipAnim*/) {
        using enum ActivationMode;
        auto& hm = ModeFor(hand);
#ifdef DEBUG
        const char* handStr = IsLeft(hand) ? "Left" : "Right";
#endif
        hm = {};
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;

        switch (ss.mode) {
            case Hold:
                hm.holdActive = true;
                if (hm.wantAutoAttack) {
                    hm.autoCastPhase = AutoCastPhase::StartRequested;
                }
                MAGIC_DEBUG_LOG("[State] EnterHand: hand={} mode=Hold wantAutoAttack={}", handStr, hm.wantAutoAttack);
                break;

            case Automatic:
                hm.autoActive = true;
                hm.wantAutoAttack = true;
                hm.autoCastPhase = AutoCastPhase::StartRequested;
                MAGIC_DEBUG_LOG("[State] EnterHand: hand={} mode=Automatic", handStr);
                break;

            case Press:
                hm.pressActive = true;
                if (hm.wantAutoAttack) {
                    hm.autoActive = true;
                    hm.autoCastPhase = AutoCastPhase::StartRequested;
                }
                MAGIC_DEBUG_LOG("[State] EnterHand: hand={} mode=Press wantAutoAttack={}", handStr, hm.wantAutoAttack);
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
            out.shoutSettings = Config::MagicConfigAdapter::Get().GetOrCreateSpellSettings(out.shoutID, out.shoutForm);

            out.needsSkipEquipVars = EnsureActiveWithSnapshot(player, slot, false);
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
            _session.activeShoutID = out.shoutID;
            _session.activeLeftID = 0;
            _session.activeRightID = 0;

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

        if (out.hasRight)
            out.rightSettings = Config::MagicConfigAdapter::Get().GetOrCreateSpellSettings(out.rightID, out.rightSpell);
        if (out.hasLeft)
            out.leftSettings = Config::MagicConfigAdapter::Get().GetOrCreateSpellSettings(out.leftID, out.leftSpell);

        out.needsSkipEquipVars = EnsureActiveWithSnapshot(player, slot);
        _session.modeSpellRight = out.rightSpell;
        _session.modeSpellLeft = out.leftSpell;
        _session.activeLeftID = out.leftID;
        _session.activeRightID = out.rightID;
        _session.activeShoutID = 0;

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

        SlotPressAction action{};

        using enum Hand;
        using enum ActivationMode;

        auto mergeExitIntoAction = [&](StateExitResult&& src) {
            if (!action.leftAttack) action.leftAttack = src.leftAttack;
            if (!action.rightAttack) action.rightAttack = src.rightAttack;
            if (!action.shout) action.shout = src.shout;

            if (!action.restorePlan && src.restorePlan) {
                action.restorePlan = std::move(src.restorePlan);
            }

            action.finalizeAfterController = action.finalizeAfterController || src.finalizeAfterController;

            action.resetShoutAfterController = action.resetShoutAfterController || src.resetShoutAfterController;
        };

        auto mergeOverwriteIntoAction = [&](const PrepareOverwriteResult& src) {
            if (!action.leftAttack && src.leftAttack) {
                action.leftAttack = StopDispatchIntent{src.leftAttack->heldSecs};
            }
            if (!action.rightAttack && src.rightAttack) {
                action.rightAttack = StopDispatchIntent{src.rightAttack->heldSecs};
            }
        };

        auto mergeDisableIntoAction = [&](Hand hand, const DisableHandResult& src) {
            if (!src.attack) return;

            if (IsLeft(hand)) {
                if (!action.leftAttack) {
                    action.leftAttack = StopDispatchIntent{src.attack->heldSecs};
                }
            } else {
                if (!action.rightAttack) {
                    action.rightAttack = StopDispatchIntent{src.attack->heldSecs};
                }
            }
        };

        if (Slots::IsShoutSlot(slot)) {
            if (_session.active && slot == _session.activeSlot && _shout.modeShoutID != 0) {
                if (_shout.finished) return action;

                const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
                if (ss && ss->mode == Press) {
                    MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout Press toggle -> StopShoutPress + finish");

                    if (_shout.held) {
                        const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                        action.shout = StopDispatchIntent{held};
                        _shout.held = false;
                        _shout.heldSecs = 0.f;
                    }

                    _shout.finished = true;
                    mergeExitIntoAction(TryFinalizeExit());
                }
                return action;
            }

            if (_session.active && slot != _session.activeSlot) {
                if (!CanOverwriteNow()) return action;

                mergeOverwriteIntoAction(PrepareForOverwriteToSlot(slot));
            }

            SlotEntry e{};
            if (!PrepareSlotEntry(slot, e)) return action;
            action.needsSkipEquipVars = e.needsSkipEquipVars;

            if ((e.shoutSettings.mode == Hold || e.shoutSettings.mode == Automatic) && !_shout.isPower &&
                e.player->GetVoiceRecoveryTime() > 0.f) {
                MAGIC_DEBUG_LOG("[State] OnSlotPressed: shout on cooldown -> early exit");
                _shout.finished = true;
                mergeExitIntoAction(TryFinalizeExit());
                return action;
            }

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: shoutID={:#010x} isPower={} mode={}", e.shoutID, _shout.isPower,
                            static_cast<int>(std::to_underlying(e.shoutSettings.mode)));

            _restore.dirtyShout = true;
            _shout.held = true;
            _shout.heldSecs = 0.f;
            if (e.shoutSettings.mode == Automatic) _shout.powerAutoSecs = 0.f;

            action.shoutToEquip = e.shoutForm;
            action.startShoutDispatch = true;
            return action;
        }

        if (_session.active && slot == _session.activeSlot) {
            const bool needL = (_session.modeSpellLeft != nullptr);
            const bool needR = (_session.modeSpellRight != nullptr);
            const bool pressL = needL && _left.mode == Press && _left.pressActive;
            const bool pressR = needR && _right.mode == Press && _right.pressActive;

            if (!pressL && !pressR) return action;

            MAGIC_DEBUG_LOG("[State] OnSlotPressed: active slot pressed again -> pressL={} pressR={}", pressL, pressR);

            action.result = SlotPressResult::Deactivated;

            if (pressL && pressR) {
                const float finishedL = FinishHand(Left);
                const float finishedR = FinishHand(Right);

                if (finishedL != -1.f) action.leftAttack = StopDispatchIntent{finishedL};
                if (finishedR != -1.f) action.rightAttack = StopDispatchIntent{finishedR};

                mergeExitIntoAction(ExitAllNow());
                return action;
            }

            if (pressL) {
                const float finishedL = FinishHand(Left);
                if (finishedL != -1.f) action.leftAttack = StopDispatchIntent{finishedL};
            }

            if (pressR) {
                const float finishedR = FinishHand(Right);
                if (finishedR != -1.f) action.rightAttack = StopDispatchIntent{finishedR};
            }

            mergeExitIntoAction(TryFinalizeExit());
            return action;
        }

        if (_session.active && slot != _session.activeSlot) {
            if (!CanOverwriteNow()) return action;

            mergeOverwriteIntoAction(PrepareForOverwriteToSlot(slot));
        }

        if (const auto afford = ComputeSlotAffordability(slot); afford.hasSpells && !afford.canCast) {
            if (_session.active) {
                mergeExitIntoAction(ExitAllNow());
            }
            return action;
        }

        SlotEntry e{};
        if (!PrepareSlotEntry(slot, e)) return action;
        action.needsSkipEquipVars = e.needsSkipEquipVars;

        _session.isDualCasting = false;
        if (e.hasRight && e.hasLeft && e.rightID == e.leftID && e.rightSettings.autoAttack &&
            e.leftSettings.autoAttack && GetDualCastCostMultiplier(e.player, e.rightSpell) > 2.f) {
            _session.isDualCasting = true;
        }

        if (!e.hasRight) {
            mergeDisableIntoAction(Right, DisableHand(Right));
            SetModeSpellsFromHand(Right, nullptr);
        }

        if (!e.hasLeft) {
            mergeDisableIntoAction(Left, DisableHand(Left));
            SetModeSpellsFromHand(Left, nullptr);
        }

        if (!e.hasLeft && !e.hasRight) {
            mergeExitIntoAction(ExitAllNow());
            return action;
        }

        if (e.hasRight) {
            action.spellsToEquip.push_back({e.rightSpell, Right});
            MarkDirty(Right);
        }

        if (e.hasLeft) {
            action.spellsToEquip.push_back({e.leftSpell, Left});
            MarkDirty(Left);
            if (!e.hasRight && SpellClassify::IsTwoHandedSpell(e.leftSpell)) {
                MarkDirty(Right);
            }
        }

        MAGIC_DEBUG_LOG(
            "[State] OnSlotPressed: preparing equip - hasLeft={} leftID={:#010x} hasRight={} rightID={:#010x} "
            "isDualCasting={}",
            e.hasLeft, e.leftID, e.hasRight, e.rightID, _session.isDualCasting);

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

    AttackEnabledResult MagicState::OnEquipComplete() {
        MAGIC_DEBUG_LOG("[State] OnEquipComplete: left(auto={} phase={}) right(auto={} phase={})",
                        _left.autoActive, static_cast<int>(_left.autoCastPhase),
                        _right.autoActive, static_cast<int>(_right.autoCastPhase));
        _inSlotSetup = false;

        auto* player = GetPlayer();
        if (!player) return {};

        AttackEnabledResult result;
        using enum Hand;
        auto tryStart = [&](Hand hand, bool& dispatchFlag) {
            auto& hm = ModeFor(hand);
            if (hm.autoCastPhase == AutoCastPhase::StartRequested) {
                dispatchFlag = RequestAutoAttackStart(hand, false);
            } else {
                MAGIC_DEBUG_LOG("[State] OnEquipComplete: hand={} skip dispatch - phase={}",
                                IsLeft(hand) ? "Left" : "Right", static_cast<int>(hm.autoCastPhase));
            }
        };
        tryStart(Left, result.dispatchLeft);
        tryStart(Right, result.dispatchRight);

        MAGIC_DEBUG_LOG("[State] OnEquipComplete: dispatchLeft={} dispatchRight={}", result.dispatchLeft,
                        result.dispatchRight);
        return result;
    }

    void MagicState::NotifyUnexpectedUnequip(RE::TESBoundObject* base) {
        if (!_inSlotSetup || !base) return;
        const bool exists =
            std::ranges::any_of(_restore.prevExtraEquipped, [&](auto const& e) { return e.base == base; });
        if (!exists) {
            _restore.prevExtraEquipped.push_back({base, nullptr});
            MAGIC_DEBUG_LOG("[State] NotifyUnexpectedUnequip: base={:#010x} (overlay side effect)", base->GetFormID());
        }
    }

    StateExitResult MagicState::OnSlotReleased(int slot) {
        StateExitResult result{};

        MAGIC_DEBUG_LOG(
            "[State] OnSlotReleased: slot={} active={} activeSlot={} modeShoutID={:#010x} isPower={} held={}", slot,
            _session.active, _session.activeSlot, _shout.modeShoutID, _shout.isPower, _shout.held);

        if (!_session.active || slot != _session.activeSlot) return result;

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

        if (_shout.modeShoutID != 0) {
            const auto ss = Config::MagicConfigAdapter::Get().GetSpellSettings(_shout.modeShoutID);
            const auto mode = ss ? ss->mode : ActivationMode::Hold;

            MAGIC_DEBUG_LOG("[State] OnSlotReleased: shout path mode={}", static_cast<int>(std::to_underlying(mode)));

            if (mode == ActivationMode::Hold) {
                MAGIC_DEBUG_LOG("[State] StopShoutPress: held={} heldSecs={:.3f} modeShoutID={:#010x}", _shout.held,
                                _shout.heldSecs, _shout.modeShoutID);

                if (_shout.held) {
                    const float held = (_shout.heldSecs > 0.f) ? _shout.heldSecs : 0.1f;
                    result.shout = StopDispatchIntent{held};
                    _shout.held = false;
                    _shout.heldSecs = 0.f;
                }

                if (_shout.isPower) {
                    MAGIC_DEBUG_LOG("[State] OnSlotReleased: power Hold release -> finishing + TryFinalizeExit");

                    _shout.finished = true;
                    merge(TryFinalizeExit());
                } else {
                    MAGIC_DEBUG_LOG("[State] OnSlotReleased: shout Hold release -> waitingStopEvent");
                    _shout.waitingStopEvent = true;
                }
            }

            return result;
        }

        using enum Hand;
        auto handleHoldRelease = [&](Hand hand) {
            auto& hm = ModeFor(hand);
            if (!hm.holdActive) return;

            hm.holdActive = false;

            const auto id = Slots::GetSlotSpell(_session.activeSlot, hand);
            const auto* spell = id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr;

            if (!spell || spell->GetChargeTime() <= 0.f) {
                const float finished = FinishHand(hand);
                if (finished != -1.f) {
                    if (IsLeft(hand))
                        result.leftAttack = StopDispatchIntent{finished};
                    else
                        result.rightAttack = StopDispatchIntent{finished};
                }
                return;
            }

            const auto src =
                IsLeft(hand) ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;

            if (auto const* caster = GetMagicCaster(GetPlayer(), src); !IsChargeComplete(caster, spell)) {
                const float finished = FinishHand(hand);
                if (finished != -1.f) {
                    if (IsLeft(hand))
                        result.leftAttack = StopDispatchIntent{finished};
                    else
                        result.rightAttack = StopDispatchIntent{finished};
                }
                return;
            }

            if (!hm.wantAutoAttack) {
                const float finished = FinishHand(hand);
                if (finished != -1.f) {
                    if (IsLeft(hand))
                        result.leftAttack = StopDispatchIntent{finished};
                    else
                        result.rightAttack = StopDispatchIntent{finished};
                }
                return;
            }

            if (_aa.Held(hand)) {
                const float held = (_aa.Secs(hand) > 0.f) ? _aa.Secs(hand) : 0.1f;

                MAGIC_DEBUG_LOG("[State] StopAutoAttack: hand={} heldSecs={:.3f}", IsLeft(hand) ? "Left" : "Right",
                                held);

                if (IsLeft(hand))
                    result.leftAttack = StopDispatchIntent{held};
                else
                    result.rightAttack = StopDispatchIntent{held};

                _aa.Held(hand) = false;
                _aa.Secs(hand) = 0.f;
            }

            hm.holdFiredAndWaitingCastStop = true;
        };

        handleHoldRelease(Left);
        handleHoldRelease(Right);

        merge(TryFinalizeExit());
        return result;
    }
}