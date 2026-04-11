#include "ExclusiveTracker.h"

#include <utility>

#include "Input/HotkeyMatcher.h"
#include "Input/ReplaySystem.h"
#include "PCH.h"
#include "State/State.h"

namespace Input::detail {

    namespace {

        inline void AtomicFetchOrU64(std::atomic<std::uint64_t>& a, std::uint64_t bits,
                                     std::memory_order order = std::memory_order_relaxed) {
            std::uint64_t cur = a.load(order);
            while (!a.compare_exchange_weak(cur, (cur | bits), order, order));
        }

        template <class DownArr, class KeepArr, class CodesArr>
        inline void MarkKeepIfDown(const CodesArr& codes, const DownArr& down, KeepArr& keep) {
            for (int code : codes) {
                if (code < 0 || code >= kMaxCode) continue;
                const auto idx = static_cast<std::size_t>(code);
                if (down[idx].load(std::memory_order_relaxed)) keep[idx] = true;
            }
        }

        bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                      bool rawNow, float dt, ExclusiveStore& excl, SlotEdgeStore& slots,
                                      const KeyStateStore& keys, ReplayArr& replay, RetainedArr& retained,
                                      DeferredVec& deferred, const IntegratedMagic::Config::IPatchSettings& patches) {
            const auto s = static_cast<std::size_t>(slot);

            excl.prevRawKbDown[s] = kbNow;
            excl.prevRawGpDown[s] = gpNow;

            const bool requireExcl = patches.RequireExclusiveHotkey();

            {
                const bool anyComboNow = AnyComboKeyDown(hk.kb, keys.kbDown) || AnyComboKeyDown(hk.gp, keys.gpDown);
                const bool prevAnyDown = excl.prevAnyKeyDown[s];
                excl.prevAnyKeyDown[s] = anyComboNow;

                if (!anyComboNow) {
                    excl.simWindowActive[s] = false;
                    excl.simWindowRemaining[s] = 0.f;
                } else if (anyComboNow && !prevAnyDown) {
                    excl.simWindowActive[s] = true;
                    excl.simWindowRemaining[s] = kPressBothAtSameTimeWindowSec;
#ifdef DEBUG
                    spdlog::info("[Input] ComputeAcceptedExclusive: slot={} sim-window OPENED ({:.2f}s)", slot,
                                 kPressBothAtSameTimeWindowSec);
#endif
                } else if (excl.simWindowActive[s]) {
                    excl.simWindowRemaining[s] -= dt;
                    if (excl.simWindowRemaining[s] <= 0.f) {
                        excl.simWindowActive[s] = false;
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} sim-window EXPIRED", slot);
#endif
                    }
                }
            }

            if (prevAccepted) {
                const bool wasDeactivated = excl.deactivatedThisPress[s];
                DiscardExclusivePending(s, excl, replay, retained, deferred);
                if (wasDeactivated) return false;
                return rawNow;
            }

            if (HasExclusivePending(s, excl)) {
                const auto src = excl.pendingSrc[s];
                const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;
                const bool srcIsMulti = (src == PendingSrc::Kb) ? slots.slotIsKbMultiKey[s] : slots.slotIsGpMultiKey[s];
                const bool simPatch = patches.PressBothAtSame() && srcIsMulti;

                if (requireExcl && srcIsMulti) {
                    const bool stillExcl =
                        (src == PendingSrc::Kb)
                            ? ComboExclusiveNow(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                            : ComboExclusiveNow(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    if (!stillExcl) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} pending CANCELLED (no longer exclusive)", slot);
#endif
                        ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                        return false;
                    }
                }

                if (srcIsMulti) {
                    if (stillDown && !excl.fullComboSeen[s]) {
                        if (simPatch && !excl.simWindowActive[s]) {
#ifdef DEBUG
                            spdlog::info(
                                "[Input] ComputeAcceptedExclusive: slot={} full combo REJECTED by sim-window (expired)",
                                slot);
#endif
                            ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                            return false;
                        }
                        excl.fullComboSeen[s] = true;
                        excl.pendingTimer[s] = kFilterReplayDelaySec;
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key full combo seen, timer reset to "
                            "{:.3f}s",
                            slot, kFilterReplayDelaySec);
#endif
                    }

                    if (!stillDown) {
                        if (excl.fullComboSeen[s]) {
#ifdef DEBUG
                            spdlog::info(
                                "[Input] ComputeAcceptedExclusive: slot={} multi-key released after full combo -> "
                                "ACCEPTED",
                                slot);
#endif
                            DiscardExclusivePending(s, excl, replay, retained, deferred);
                            return true;
                        }

                        const bool anyHeld = (src == PendingSrc::Gp) ? AnyComboKeyDown(hk.gp, keys.gpDown)
                                                                     : AnyComboKeyDown(hk.kb, keys.kbDown);
                        if (anyHeld) {
                            if (simPatch) {
                                excl.pendingTimer[s] -= dt;
                                if (excl.pendingTimer[s] <= 0.0f) {
#ifdef DEBUG
                                    spdlog::info(
                                        "[Input] ComputeAcceptedExclusive: slot={} multi-key partial hold TIMEOUT -> "
                                        "Cancelled",
                                        slot);
#endif
                                    ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                                }
                            }
                            return false;
                        }
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key all released without full combo -> "
                            "Cancelled",
                            slot);
#endif
                        ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
                        return false;
                    }

                    excl.pendingTimer[s] -= dt;
                    if (excl.pendingTimer[s] <= 0.0f) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key timer elapsed -> Success (held down)",
                            slot);
#endif
                        ClearExclusivePending(s, ClearReason::Success, excl, replay, retained, deferred);
                        return true;
                    }
                    return false;

                } else {
                    if (!stillDown) {
                        const bool wasDeactivated = excl.deactivatedThisPress[s];
                        DiscardExclusivePending(s, excl, replay, retained, deferred);
                        if (wasDeactivated) {
#ifdef DEBUG
                            spdlog::info(
                                "[Input] ComputeAcceptedExclusive: slot={} single-key released -> IGNORED "
                                "(deactivatedThisPress)",
                                slot);
#endif
                            return false;
                        }
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} single-key released -> ACCEPTED", slot);
#endif
                        return true;
                    }
                    return false;
                }
            }

            // nenhum pending — tentar iniciar novo
            if (!rawNow) return false;

            if (kbNow) {
                const bool kbIsMulti = slots.slotIsKbMultiKey[s];
                if (!requireExcl || ComboExclusiveNow(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)) {
                    if (const bool kbSimPatch = patches.PressBothAtSame() && kbIsMulti;
                        kbSimPatch && !excl.simWindowActive[s]) {
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} kb REJECTED sim-window not active",
                                     slot);
#endif
                        return false;
                    }
                    excl.pendingSrc[s] = PendingSrc::Kb;
                    excl.pendingTimer[s] = kFilterReplayDelaySec;
                    excl.fullComboSeen[s] = false;
                    return false;
                }
            }

            if (gpNow) {
                const bool gpIsMulti = slots.slotIsGpMultiKey[s];
                if (!requireExcl || ComboExclusiveNow(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera)) {
                    if (const bool gpSimPatch = patches.PressBothAtSame() && gpIsMulti;
                        gpSimPatch && !excl.simWindowActive[s]) {
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} gp REJECTED sim-window not active",
                                     slot);
#endif
                        return false;
                    }
                    excl.pendingSrc[s] = PendingSrc::Gp;
                    excl.pendingTimer[s] = kFilterReplayDelaySec;
                    excl.fullComboSeen[s] = false;
                    return false;
                }
            }

            return false;
        }

    }  // namespace

    void DiscardExclusivePending(std::size_t s, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                                 DeferredVec& deferred) {
#ifdef DEBUG
        if (excl.pendingSrc[s] != PendingSrc::None || !retained[s].empty()) {
            spdlog::info("[Input] DiscardExclusivePending: slot={} (had pending src={} retained={})", s,
                         static_cast<int>(std::to_underlying(excl.pendingSrc[s])), retained[s].size());
        }
#endif
        excl.filterWindowActive[s] = false;
        excl.filterWindowTimer[s] = 0.f;
        retained[s].clear();
        ClearDeferredReplayEventsForSlot(s, deferred);
        excl.pendingSrc[s] = PendingSrc::None;
        excl.pendingTimer[s] = 0.0f;
        excl.fullComboSeen[s] = false;
        ResetReplayState(s, replay);
    }

    void ClearExclusivePending(std::size_t s, ClearReason reason, ExclusiveStore& excl, ReplayArr& replay,
                               RetainedArr& retained, DeferredVec& deferred) {
#ifdef DEBUG
        const char* reasonStr = (reason == ClearReason::Success)   ? "Success"
                                : (reason == ClearReason::Timeout) ? "Timeout"
                                                                   : "Cancelled";
        spdlog::info("[Input] ClearExclusivePending: slot={} reason={} retainedEvents={}", s, reasonStr,
                     retained[s].size());
#endif
        if (reason != ClearReason::Success) {
            ClearDeferredReplayEventsForSlot(s, deferred);
            ResetReplayState(s, replay);
            if (excl.filterWindowActive[s]) {
                excl.filterWindowActive[s] = false;
                excl.filterWindowTimer[s] = 0.f;
                for (auto const& ev : retained[s]) {
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ClearExclusivePending: slot={} queue replay dev={} value={:.2f} heldSecs={:.3f}", s,
                        static_cast<int>(ev.dev), ev.value, ev.heldSecs);
#endif
                    QueueDeferredReplayEvent(s, ev, deferred);
                }
            }
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
        } else {
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            ClearDeferredReplayEventsForSlot(s, deferred);
            ResetReplayState(s, replay);
        }
        retained[s].clear();
        excl.pendingSrc[s] = PendingSrc::None;
        excl.pendingTimer[s] = 0.0f;
        excl.fullComboSeen[s] = false;
    }

    void ClearEdgeStateOnly(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                            DeferredVec& deferred, KeyStateStore& keys) {
#ifdef DEBUG
        spdlog::info(
            "[Input] ClearEdgeStateOnly: clearing edge state without discarding pending or resetting replay state");
#endif
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            slots.slotDown[s].store(false, std::memory_order_relaxed);
            slots.slotWasAccepted[s] = false;
            excl.fullComboSeen[s] = false;
            excl.prevRawKbDown[s] = false;
            excl.prevRawGpDown[s] = false;
            excl.prevAnyKeyDown[s] = false;
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            DiscardExclusivePending(s, excl, replay, retained, deferred);
        }
        slots.pressedMask.store(0uLL, std::memory_order_relaxed);
        slots.releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void ClearLikelyStuckKeysAfterMenuClose(KeyStateStore& keys, SlotEdgeStore& slots, ExclusiveStore& excl,
                                            const HotkeyCacheStore& cache, ReplayArr& replay, RetainedArr& retained,
                                            DeferredVec& deferred) {
        std::array<bool, kMaxCode> keepKb{};
        std::array<bool, kMaxCode> keepGp{};
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = cache.slots[static_cast<std::size_t>(slot)];
            MarkKeepIfDown(hk.kb, keys.kbDown, keepKb);
            MarkKeepIfDown(hk.gp, keys.gpDown, keepGp);
        }
        for (int i = 0; i < kMaxCode; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (!keepKb[idx]) keys.kbDown[idx].store(false, std::memory_order_relaxed);
            if (!keepGp[idx]) keys.gpDown[idx].store(false, std::memory_order_relaxed);
        }
        keys.kbDown[static_cast<std::size_t>(kDIK_Escape)].store(false, std::memory_order_relaxed);
        ClearEdgeStateOnly(slots, excl, replay, retained, deferred, keys);
    }

    void ResetExclusiveState(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                             DeferredVec& deferred) {
#ifdef DEBUG
        spdlog::info("[Input] ResetExclusiveState: resetting all exclusive pending and edge state");
#endif
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            excl.prevRawKbDown[s] = false;
            excl.prevRawGpDown[s] = false;
            excl.prevAnyKeyDown[s] = false;
            excl.simWindowActive[s] = false;
            excl.simWindowRemaining[s] = 0.f;
            DiscardExclusivePending(s, excl, replay, retained, deferred);
            slots.slotDown[s].store(false, std::memory_order_relaxed);
            slots.slotWasAccepted[s] = false;
            excl.filterWindowActive[s] = false;
            excl.filterWindowTimer[s] = 0.f;
            excl.deactivatedThisPress[s] = false;
        }
        slots.pressedMask.store(0uLL, std::memory_order_relaxed);
        slots.releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void RecomputeSlotEdges(float dt, SlotEdgeStore& slots, ExclusiveStore& excl, const HotkeyCacheStore& cache,
                            const KeyStateStore& keys, const IntegratedMagic::Config::IPatchSettings& patches,
                            ReplayArr& replay, RetainedArr& retained, DeferredVec& deferred) {
        TickFilterWindows(dt, excl, slots, retained, deferred);  // ← sem controller

        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const auto& hk = cache.slots[s];
            const bool kbNow = ComboDown(hk.kb, keys.kbDown);
            const bool gpNow = ComboDown(hk.gp, keys.gpDown);
            const bool rawNow = kbNow || gpNow;
            const bool prevAcc = slots.slotDown[s].load(std::memory_order_relaxed);

            bool accNow = false;
            if (patches.RequireExclusiveHotkey() || slots.slotIsMultiKey[s]) {
                accNow = ComputeAcceptedExclusive(slot, hk, prevAcc, kbNow, gpNow, rawNow, dt, excl, slots, keys,
                                                  replay, retained, deferred, patches);
            } else {
                excl.prevRawKbDown[s] = kbNow;
                excl.prevRawGpDown[s] = gpNow;
                DiscardExclusivePending(s, excl, replay, retained, deferred);
                accNow = rawNow;
            }

            if (accNow != prevAcc) {
#ifdef DEBUG
                spdlog::info("[Input] RecomputeSlotEdges: slot={} EDGE {} (kb={} gp={} exclusive={} multiKey={})", slot,
                             accNow ? "PRESSED" : "RELEASED", kbNow, gpNow, patches.RequireExclusiveHotkey(),
                             slots.slotIsMultiKey[s]);
#endif
                slots.slotDown[s].store(accNow, std::memory_order_relaxed);
                AtomicFetchOrU64(accNow ? slots.pressedMask : slots.releasedMask, (1uLL << slot));
            }

            if (accNow)
                slots.slotWasAccepted[s] = true;
            else if (!rawNow) {
                const auto& ms = IntegratedMagic::MagicState::Get();
                if (!ms.IsActive() || ms.ActiveSlot() != slot) slots.slotWasAccepted[s] = false;
            }
        }
    }

    void TickFilterWindows(float dt, ExclusiveStore& excl, SlotEdgeStore& slots, RetainedArr& retained,
                           DeferredVec& deferred) {
        const int n = slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            if (!excl.filterWindowActive[s]) continue;
            excl.filterWindowTimer[s] -= dt;
            if (excl.filterWindowTimer[s] > 0.f) continue;

            excl.filterWindowActive[s] = false;
            for (auto const& ev : retained[s]) QueueDeferredReplayEvent(s, ev, deferred);
            retained[s].clear();
#ifdef DEBUG
            spdlog::info("[Input] TickFilterWindows: slot={} filter expired, flushed {} events to replay", slot,
                         deferred.size());
#endif
        }
    }

}