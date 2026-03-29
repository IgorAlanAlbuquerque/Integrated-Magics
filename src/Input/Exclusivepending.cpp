#include <utility>

#include "ExclusivePending.h"

#include "Config/Config.h"
#include "HotkeyCache.h"
#include "PCH.h"
#include "ReplaySystem.h"

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
                                      bool rawNow, float dt) {
            const auto s = static_cast<std::size_t>(slot);

            const bool kbPrev = g_prevRawKbDown[s];
            const bool gpPrev = g_prevRawGpDown[s];
            g_prevRawKbDown[s] = kbNow;
            g_prevRawGpDown[s] = gpNow;

            const auto& cfg = IntegratedMagic::GetMagicConfig();
            const bool requireExcl = cfg.requireExclusiveHotkeyPatch;

            {
                const bool anyComboNow = AnyComboKeyDown(hk.kb, g_kbDown) || AnyComboKeyDown(hk.gp, g_gpDown);
                const bool prevAnyDown = g_prevAnyKeyDown[s];
                g_prevAnyKeyDown[s] = anyComboNow;

                if (!anyComboNow) {
                    g_simWindowActive[s] = false;
                    g_simWindowRemaining[s] = 0.f;
                } else if (anyComboNow && !prevAnyDown) {
                    if (g_replay[s].skipNextSimWindowOpen) {
                        g_replay[s].skipNextSimWindowOpen = false;
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} sim-window SKIPPED (re-enqueued DOWN)",
                                     slot);
#endif
                    } else {
                        g_simWindowActive[s] = true;
                        g_simWindowRemaining[s] = kExclusiveConfirmDelaySec;
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} sim-window OPENED ({:.2f}s)", slot,
                                     kExclusiveConfirmDelaySec);
#endif
                    }
                } else if (g_simWindowActive[s]) {
                    g_simWindowRemaining[s] -= dt;
                    if (g_simWindowRemaining[s] <= 0.f) {
                        g_simWindowActive[s] = false;
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} sim-window EXPIRED", slot);
#endif
                    }
                }
            }

            if (prevAccepted) {
                DiscardExclusivePending(s);
                return rawNow;
            }

            if (HasExclusivePending(s)) {
                const auto src = g_exclusivePendingSrc[s];
                const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;

                const bool srcIsMulti = (src == PendingSrc::Kb) ? g_slotIsKbMultiKey[s] : g_slotIsGpMultiKey[s];
                const bool simPatch = cfg.pressBothAtSamePatch && srcIsMulti;

                if (requireExcl && srcIsMulti) {
                    const bool stillExcl =
                        (src == PendingSrc::Kb)
                            ? ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                            : ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    if (!stillExcl) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} pending CANCELLED (no longer exclusive)", slot);
#endif
                        ClearExclusivePending(s, ClearReason::Cancelled);
                        return false;
                    }
                }

                if (srcIsMulti) {
                    if (stillDown && !g_slotFullComboSeen[s]) {
                        if (simPatch && !g_simWindowActive[s]) {
#ifdef DEBUG
                            spdlog::info(
                                "[Input] ComputeAcceptedExclusive: slot={} full combo REJECTED by sim-window (expired)",
                                slot);
#endif
                            ClearExclusivePending(s, ClearReason::Cancelled);
                            return false;
                        }
                        g_slotFullComboSeen[s] = true;
                        g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key full combo seen, timer reset to "
                            "{:.3f}s",
                            slot, kExclusiveConfirmDelaySec);
#endif
                    }

                    if (!stillDown) {
                        if (g_slotFullComboSeen[s]) {
#ifdef DEBUG
                            spdlog::info(
                                "[Input] ComputeAcceptedExclusive: slot={} multi-key released after full combo -> "
                                "ACCEPTED",
                                slot);
#endif
                            DiscardExclusivePending(s);
                            return true;
                        }

                        if (const bool anyHeld = (src == PendingSrc::Gp) ? AnyComboKeyDown(hk.gp, g_gpDown)
                                                                         : AnyComboKeyDown(hk.kb, g_kbDown);
                            anyHeld) {
                            g_exclusivePendingTimer[s] -= dt;
                            if (g_exclusivePendingTimer[s] <= 0.0f) {
#ifdef DEBUG
                                spdlog::info(
                                    "[Input] ComputeAcceptedExclusive: slot={} multi-key partial hold TIMEOUT -> "
                                    "Cancelled",
                                    slot);
#endif
                                ClearExclusivePending(s, ClearReason::Cancelled);
                            }
                            return false;
                        }
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key all released without full combo -> "
                            "Cancelled",
                            slot);
#endif
                        ClearExclusivePending(s, ClearReason::Cancelled);
                        return false;
                    }

                    g_exclusivePendingTimer[s] -= dt;
                    if (g_exclusivePendingTimer[s] <= 0.0f) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key timer elapsed -> Success (held down)",
                            slot);
#endif
                        ClearExclusivePending(s, ClearReason::Success);
                        return true;
                    }
                    return false;

                } else {
                    if (!stillDown) {
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} single-key released -> ACCEPTED", slot);
#endif
                        DiscardExclusivePending(s);
                        return true;
                    }
                    g_exclusivePendingTimer[s] -= dt;
                    if (g_exclusivePendingTimer[s] <= 0.0f) {
#ifdef DEBUG
                        spdlog::info("[Input] ComputeAcceptedExclusive: slot={} single-key timer elapsed -> Success",
                                     slot);
#endif
                        ClearExclusivePending(s, ClearReason::Success);
                        return true;
                    }
                    return false;
                }
            }

            const bool kbEdge = kbNow && !kbPrev;
            const bool gpEdge = gpNow && !gpPrev;

            if (kbEdge) {
                const bool kbIsMulti = g_slotIsKbMultiKey[s];
                const bool kbExclOk = !requireExcl || !kbIsMulti ||
                                      ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);
                if (kbExclOk) {
                    if (const bool kbSimPatch = cfg.pressBothAtSamePatch && kbIsMulti;
                        kbSimPatch && !g_simWindowActive[s]) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} KB edge REJECTED by sim-window "
                            "(expired/inactive)",
                            slot);
#endif
                        return false;
                    }
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ComputeAcceptedExclusive: slot={} KB edge detected, starting exclusive pending "
                        "(kbIsMulti={})",
                        slot, kbIsMulti);
#endif
                    g_exclusivePendingSrc[s] = PendingSrc::Kb;
                    g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
                    if (kbIsMulti) g_slotFullComboSeen[s] = true;
                    return false;
                }
            }

            if (gpEdge) {
                const bool gpIsMulti = g_slotIsGpMultiKey[s];
                const bool gpExclOk = !requireExcl || !gpIsMulti ||
                                      ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                if (gpExclOk) {
                    if (const bool gpSimPatch = cfg.pressBothAtSamePatch && gpIsMulti;
                        gpSimPatch && !g_simWindowActive[s]) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} GP edge REJECTED by sim-window "
                            "(expired/inactive)",
                            slot);
#endif
                        return false;
                    }
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ComputeAcceptedExclusive: slot={} GP edge detected, starting exclusive pending "
                        "(gpIsMulti={})",
                        slot, gpIsMulti);
#endif
                    g_exclusivePendingSrc[s] = PendingSrc::Gp;
                    g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
                    if (gpIsMulti) g_slotFullComboSeen[s] = true;
                    return false;
                }
            }

            return false;
        }
    }

    void DiscardExclusivePending(std::size_t s) {
        if (g_exclusivePendingSrc[s] != PendingSrc::None || !g_retainedEvents[s].empty()) {
#ifdef DEBUG
            spdlog::info("[Input] DiscardExclusivePending: slot={} (had pending src={} retained={})", s,
                         static_cast<int>(std::to_underlying(g_exclusivePendingSrc[s])), g_retainedEvents[s].size());
#endif
        }
        g_retainedEvents[s].clear();
        ClearDeferredReplayEventsForSlot(s);
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
        g_slotFullComboSeen[s] = false;
        ResetReplayState(s);
    }

    void ClearExclusivePending(std::size_t s, ClearReason reason) {
#ifdef DEBUG
        const char* reasonStr = (reason == ClearReason::Success)   ? "Success"
                                : (reason == ClearReason::Timeout) ? "Timeout"
                                                                   : "Cancelled";
        spdlog::info("[Input] ClearExclusivePending: slot={} reason={} retainedEvents={}", s, reasonStr,
                     g_retainedEvents[s].size());
#endif
        if (reason != ClearReason::Success) {
            g_simWindowActive[s] = false;
            g_simWindowRemaining[s] = 0.f;
            ClearDeferredReplayEventsForSlot(s);
            ResetReplayState(s);
            for (auto const& ev : g_retainedEvents[s]) {
#ifdef DEBUG
                spdlog::info("[Input] ClearExclusivePending: slot={} queue replay dev={} value={:.2f} heldSecs={:.3f}",
                             s, static_cast<int>(ev.dev), ev.value, ev.heldSecs);
#endif
                QueueDeferredReplayEvent(s, ev);
            }
        } else {
            ClearDeferredReplayEventsForSlot(s);
            ResetReplayState(s);
        }
        g_retainedEvents[s].clear();
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
        g_slotFullComboSeen[s] = false;
    }

    void ClearEdgeStateOnly() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_slotWasAccepted[s] = false;
            g_slotFullComboSeen[s] = false;
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            g_prevAnyKeyDown[s] = false;
            g_simWindowActive[s] = false;
            g_simWindowRemaining[s] = 0.f;
            DiscardExclusivePending(s);
        }
        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void ClearLikelyStuckKeysAfterMenuClose() {
        std::array<bool, kMaxCode> keepKb{};
        std::array<bool, kMaxCode> keepGp{};
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];
            MarkKeepIfDown(hk.kb, g_kbDown, keepKb);
            MarkKeepIfDown(hk.gp, g_gpDown, keepGp);
        }
        for (int i = 0; i < kMaxCode; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (!keepKb[idx]) g_kbDown[idx].store(false, std::memory_order_relaxed);
            if (!keepGp[idx]) g_gpDown[idx].store(false, std::memory_order_relaxed);
        }
        g_kbDown[static_cast<std::size_t>(kDIK_Escape)].store(false, std::memory_order_relaxed);
        ClearEdgeStateOnly();
    }

    void ResetExclusiveState() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            g_prevAnyKeyDown[s] = false;
            g_simWindowActive[s] = false;
            g_simWindowRemaining[s] = 0.f;
            DiscardExclusivePending(s);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_slotWasAccepted[s] = false;
        }
        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void RecomputeSlotEdges(float dt) {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const auto& hk = g_cache[s];
            const bool kbNow = ComboDown(hk.kb, g_kbDown);
            const bool gpNow = ComboDown(hk.gp, g_gpDown);
            const bool rawNow = kbNow || gpNow;
            const bool prevAcc = g_slotDown[s].load(std::memory_order_relaxed);

            bool accNow = false;
            if (cfg.requireExclusiveHotkeyPatch || g_slotIsMultiKey[s]) {
                accNow = ComputeAcceptedExclusive(slot, hk, prevAcc, kbNow, gpNow, rawNow, dt);
            } else {
                g_prevRawKbDown[s] = kbNow;
                g_prevRawGpDown[s] = gpNow;
                DiscardExclusivePending(s);
                accNow = rawNow;
            }

            if (accNow != prevAcc) {
#ifdef DEBUG
                spdlog::info("[Input] RecomputeSlotEdges: slot={} EDGE {} (kb={} gp={} exclusive={} multiKey={})", slot,
                             accNow ? "PRESSED" : "RELEASED", kbNow, gpNow, cfg.requireExclusiveHotkeyPatch,
                             g_slotIsMultiKey[s]);
#endif
                g_slotDown[s].store(accNow, std::memory_order_relaxed);
                AtomicFetchOrU64(accNow ? g_pressedMask : g_releasedMask, (1uLL << slot));
            }

            if (accNow)
                g_slotWasAccepted[s] = true;
            else if (!rawNow)
                g_slotWasAccepted[s] = false;
        }
    }
}