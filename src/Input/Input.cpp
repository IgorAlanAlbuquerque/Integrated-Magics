#include "Input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>

#include "Config/Config.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "State/State.h"
#include "UI/HudManager.h"

namespace {
    constexpr int kMaxSlots = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
    constexpr int kMaxCode = 400;

    constexpr float kExclusiveConfirmDelaySec = 0.10f;
    constexpr int kDIK_W = 0x11;
    constexpr int kDIK_A = 0x1E;
    constexpr int kDIK_S = 0x1F;
    constexpr int kDIK_D = 0x20;
    constexpr int kDIK_Escape = 0x01;
    static_assert(kMaxSlots <= 64, "Input mask uses uint64_t, keep max slots <= 64.");

    std::atomic<int> g_slotCount{4};
    std::array<std::atomic_bool, kMaxCode> g_kbDown{};
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};
    std::array<std::atomic_bool, kMaxSlots> g_slotDown{};
    std::array<bool, kMaxSlots> g_slotWasAccepted{};
    std::atomic<std::uint64_t> g_pressedMask{0ull};
    std::atomic<std::uint64_t> g_releasedMask{0ull};
    std::array<bool, kMaxSlots> g_prevRawKbDown{};
    std::array<bool, kMaxSlots> g_slotIsMultiKey{};
    std::array<bool, kMaxSlots> g_slotFullComboSeen{};
    std::array<bool, kMaxSlots> g_prevRawGpDown{};
    std::array<float, kMaxSlots> g_exclusivePendingTimer{};

    std::array<bool, kMaxSlots> g_prevAnyKeyDown{};
    std::array<bool, kMaxSlots> g_simWindowActive{};
    std::array<float, kMaxSlots> g_simWindowRemaining{};

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };
    std::array<PendingSrc, kMaxSlots> g_exclusivePendingSrc{};

    struct ReplayState {
        bool armed{false};
        bool skipNextSimWindowOpen{false};
        RE::INPUT_DEVICE dev{RE::INPUT_DEVICE::kKeyboard};
        std::uint32_t rawIdCode{0};
        RE::BSFixedString userEvent{};
        bool valueAboveHalf{false};
    };
    std::array<ReplayState, kMaxSlots> g_replay{};

    struct SlotHotkeys {
        std::array<int, 3> kb{-1, -1, -1};
        std::array<int, 3> gp{-1, -1, -1};
    };
    std::array<SlotHotkeys, kMaxSlots> g_cache{};
    SlotHotkeys g_hudCache{};
    std::atomic_bool g_hudTogglePending{false};
    std::atomic_bool g_captureModeActive{false};

    struct RetainedEvent {
        RE::INPUT_DEVICE dev;
        std::uint32_t rawIdCode;
        RE::BSFixedString userEvent;
        float value;
        float heldSecs;
    };
    std::array<std::vector<RetainedEvent>, kMaxSlots> g_retainedEvents{};

    struct DeferredReplayEvent {
        std::size_t slot{0};
        RetainedEvent ev{};
    };
    std::vector<DeferredReplayEvent> g_deferredEvents{};

    struct CaptureState {
        std::atomic_bool captureRequested{false};
        std::atomic_int capturedEncoded{-1};
    };
    CaptureState& GetCaptureState() {
        static CaptureState st{};
        return st;
    }

    inline int ActiveSlots() {
        int n = g_slotCount.load(std::memory_order_relaxed);
        if (n < 1) n = 1;
        if (n > kMaxSlots) n = kMaxSlots;
        return n;
    }

    inline bool HasExclusivePending(std::size_t s) { return g_exclusivePendingSrc[s] != PendingSrc::None; }

    inline bool IsAllowedExtra_Keyboard_MoveOrCamera(int code) {
        switch (code) {
            case kDIK_W:
            case kDIK_A:
            case kDIK_S:
            case kDIK_D:
                return true;
            default:
                return false;
        }
    }
    inline bool IsAllowedExtra_Gamepad_MoveOrCamera(int) { return false; }

    bool AnyEnabled(const std::array<int, 3>& a) { return (a[0] != -1) || (a[1] != -1) || (a[2] != -1); }

    template <class DownArr>
    bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) return false;
        return std::ranges::all_of(combo, [&](int code) {
            if (code == -1) return true;
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    bool HasDeferredReplayForSlot(std::size_t s) {
        return std::ranges::any_of(g_deferredEvents, [s](const DeferredReplayEvent& item) { return item.slot == s; });
    }

    inline bool ComboContains(const std::array<int, 3>& combo, int code) {
        return std::ranges::find(combo, code) != combo.end();
    }

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down, AllowedFn isAllowedExtra) {
        if (!AnyEnabled(combo)) return false;
        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
            if (ComboContains(combo, code)) continue;
            if (isAllowedExtra(code)) continue;
            return false;
        }
        return true;
    }

    inline bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        if (ui->GameIsPaused()) return true;
        if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) return true;
        if (g_captureModeActive.load(std::memory_order_relaxed)) return true;
        static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString statsMenu{"StatsMenu"};
        static const RE::BSFixedString mapMenu{"MapMenu"};
        static const RE::BSFixedString journalMenu{"Journal Menu"};
        static const RE::BSFixedString favoritesMenu{"FavoritesMenu"};
        static const RE::BSFixedString containerMenu{"ContainerMenu"};
        static const RE::BSFixedString barterMenu{"BarterMenu"};
        static const RE::BSFixedString trainingMenu{"Training Menu"};
        static const RE::BSFixedString craftingMenu{"Crafting Menu"};
        static const RE::BSFixedString giftMenu{"GiftMenu"};
        static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
        static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString console{"Console"};
        static const RE::BSFixedString mcm{"Mod Configuration Menu"};
        static const RE::BSFixedString tweenMenu{"Tween Menu"};
        return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
               ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
               ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
               ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
               ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
               ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) || ui->IsMenuOpen(tweenMenu);
    }

    inline void AtomicFetchOrU64(std::atomic<std::uint64_t>& a, std::uint64_t bits,
                                 std::memory_order order = std::memory_order_relaxed) {
        std::uint64_t cur = a.load(order);
        while (!a.compare_exchange_weak(cur, (cur | bits), order, order));
    }

    enum class ClearReason { Success, Timeout, Cancelled };

    void ResetReplayState(std::size_t s) { g_replay[s] = ReplayState{}; }

    void QueueDeferredReplayEvent(std::size_t s, const RetainedEvent& ev) {
        g_deferredEvents.push_back(DeferredReplayEvent{s, ev});
    }

    void ClearDeferredReplayEventsForSlot(std::size_t s) {
        g_deferredEvents.erase(std::remove_if(g_deferredEvents.begin(), g_deferredEvents.end(),
                                              [s](const DeferredReplayEvent& item) { return item.slot == s; }),
                               g_deferredEvents.end());
    }

    bool ReplayMatchesEvent(std::size_t s, RE::INPUT_DEVICE dev, std::uint32_t rawIdCode,
                            const RE::BSFixedString& userEvent, float value) {
        const auto& rp = g_replay[s];
        if (!rp.armed) return false;
        if (rp.dev != dev) return false;
        if (rp.rawIdCode != rawIdCode) return false;
        if (rp.userEvent != userEvent) return false;
        return (value > 0.5f) == rp.valueAboveHalf;
    }

    void DrainOneDeferredReplayEvent() {
        if (g_deferredEvents.empty()) return;

        const auto item = g_deferredEvents.front();
        g_deferredEvents.erase(g_deferredEvents.begin());

        auto& rp = g_replay[item.slot];
        rp.armed = true;
        rp.dev = item.ev.dev;
        rp.rawIdCode = item.ev.rawIdCode;
        rp.userEvent = item.ev.userEvent;
        rp.valueAboveHalf = item.ev.value > 0.5f;
        rp.skipNextSimWindowOpen = rp.valueAboveHalf;

#ifdef DEBUG
        spdlog::info("[Input] Replay: slot={} dequeue dev={} value={:.2f} heldSecs={:.3f}", item.slot,
                     static_cast<int>(item.ev.dev), item.ev.value, item.ev.heldSecs);
#endif

        IntegratedMagic::detail::EnqueueRetainedEvent(item.ev.dev, item.ev.rawIdCode, item.ev.userEvent, item.ev.value,
                                                      item.ev.heldSecs);
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

    void DiscardExclusivePending(std::size_t s) {
        if (g_exclusivePendingSrc[s] != PendingSrc::None || !g_retainedEvents[s].empty()) {
#ifdef DEBUG
            spdlog::info("[Input] DiscardExclusivePending: slot={} (had pending src={} retained={})", s,
                         static_cast<int>(g_exclusivePendingSrc[s]), g_retainedEvents[s].size());
#endif
        }
        g_retainedEvents[s].clear();
        ClearDeferredReplayEventsForSlot(s);
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
        g_slotFullComboSeen[s] = false;
        ResetReplayState(s);
    }

    void ClearEdgeStateOnly() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_slotWasAccepted[s] = false;
            g_slotFullComboSeen[s] = false;
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

    template <class DownArr, class KeepArr, class CodesArr>
    inline void MarkKeepIfDown(const CodesArr& codes, const DownArr& down, KeepArr& keep) {
        for (int code : codes) {
            if (code < 0 || code >= kMaxCode) continue;
            const auto idx = static_cast<std::size_t>(code);
            if (down[idx].load(std::memory_order_relaxed)) keep[idx] = true;
        }
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

    void HandleSlotPressed(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return;
        if (!RE::PlayerCharacter::GetSingleton()) return;
#ifdef DEBUG
        spdlog::info("[Input] HandleSlotPressed: slot={}", slot);
#endif
        IntegratedMagic::MagicState::Get().OnSlotPressed(slot);
    }

    void HandleSlotReleased(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return;
#ifdef DEBUG
        spdlog::info("[Input] HandleSlotReleased: slot={}", slot);
#endif
        IntegratedMagic::MagicState::Get().OnSlotReleased(slot);
    }

    bool SlotComboDown(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return false;
        const auto& hk = g_cache[static_cast<std::size_t>(slot)];
        return ComboDown(hk.kb, g_kbDown) || ComboDown(hk.gp, g_gpDown);
    }

    template <class DownArr>
    bool AnyComboKeyDown(const std::array<int, 3>& combo, const DownArr& down) {
        return std::ranges::any_of(combo, [&](int code) {
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                  bool rawNow, float dt) {
        const auto s = static_cast<std::size_t>(slot);
        const bool isMulti = g_slotIsMultiKey[s];

        const bool kbPrev = g_prevRawKbDown[s];
        const bool gpPrev = g_prevRawGpDown[s];
        g_prevRawKbDown[s] = kbNow;
        g_prevRawGpDown[s] = gpNow;
        const bool requireExcl = IntegratedMagic::GetMagicConfig().requireExclusiveHotkeyPatch;

        const bool simPatch = IntegratedMagic::GetMagicConfig().pressBothAtSamePatch && isMulti;
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

            if (requireExcl) {
                const bool stillExcl = (src == PendingSrc::Kb)
                                           ? ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                                           : ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                if (!stillExcl) {
#ifdef DEBUG
                    spdlog::info("[Input] ComputeAcceptedExclusive: slot={} pending CANCELLED (no longer exclusive)",
                                 slot);
#endif
                    ClearExclusivePending(s, ClearReason::Cancelled);
                    return false;
                }
            }

            if (isMulti) {
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
                        "[Input] ComputeAcceptedExclusive: slot={} multi-key full combo seen, timer reset to {:.3f}s",
                        slot, kExclusiveConfirmDelaySec);
#endif
                }

                if (!stillDown) {
                    if (g_slotFullComboSeen[s]) {
#ifdef DEBUG
                        spdlog::info(
                            "[Input] ComputeAcceptedExclusive: slot={} multi-key released after full combo -> ACCEPTED",
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
                                "[Input] ComputeAcceptedExclusive: slot={} multi-key partial hold TIMEOUT -> Cancelled",
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
                    spdlog::info("[Input] ComputeAcceptedExclusive: slot={} single-key timer elapsed -> Success", slot);
#endif
                    ClearExclusivePending(s, ClearReason::Success);
                    return true;
                }
                return false;
            }
        }

        const bool kbEdge = kbNow && !kbPrev;
        const bool gpEdge = gpNow && !gpPrev;

        if (kbEdge && (!requireExcl || ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera))) {
            if (simPatch && !g_simWindowActive[s]) {
#ifdef DEBUG
                spdlog::info(
                    "[Input] ComputeAcceptedExclusive: slot={} KB edge REJECTED by sim-window (expired/inactive)",
                    slot);
#endif
                return false;
            }
#ifdef DEBUG
            spdlog::info(
                "[Input] ComputeAcceptedExclusive: slot={} KB edge detected, starting exclusive pending (isMulti={})",
                slot, isMulti);
#endif
            g_exclusivePendingSrc[s] = PendingSrc::Kb;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            if (isMulti) g_slotFullComboSeen[s] = true;
            return false;
        }
        if (gpEdge && (!requireExcl || ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera))) {
            if (simPatch && !g_simWindowActive[s]) {
#ifdef DEBUG
                spdlog::info(
                    "[Input] ComputeAcceptedExclusive: slot={} GP edge REJECTED by sim-window (expired/inactive)",
                    slot);
#endif
                return false;
            }
#ifdef DEBUG
            spdlog::info(
                "[Input] ComputeAcceptedExclusive: slot={} GP edge detected, starting exclusive pending (isMulti={})",
                slot, isMulti);
#endif
            g_exclusivePendingSrc[s] = PendingSrc::Gp;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            if (isMulti) g_slotFullComboSeen[s] = true;
            return false;
        }
        return false;
    }

    void RecomputeSlotEdges(float dt) {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];
            const bool kbNow = ComboDown(hk.kb, g_kbDown);
            const bool gpNow = ComboDown(hk.gp, g_gpDown);
            const bool rawNow = kbNow || gpNow;
            const auto s = static_cast<std::size_t>(slot);
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

    float CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();
        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
        return dt;
    }

    std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& maskAtomic) {
        while (true) {
            const int n = ActiveSlots();
            const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
            std::uint64_t curAll = maskAtomic.load(std::memory_order_relaxed);
            std::uint64_t cur = (curAll & allowed);
            if (cur == 0uLL) {
                if (curAll != 0uLL)
                    (void)maskAtomic.compare_exchange_weak(curAll, (curAll & allowed), std::memory_order_relaxed);
                return std::nullopt;
            }
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (cur & (1uLL << i)) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return std::nullopt;
            if (maskAtomic.compare_exchange_weak(curAll, (curAll & ~(1uLL << idx)), std::memory_order_relaxed))
                return idx;
        }
    }

    bool TryHandleCapture(const RE::ButtonEvent* btn, CaptureState& cap, bool& wantCapture, RE::INPUT_DEVICE dev,
                          int convertedCode) {
        if (!wantCapture || !btn->IsDown()) return false;
        if (dev == RE::INPUT_DEVICE::kKeyboard && convertedCode == kDIK_Escape) return false;
        int encoded = -1;

        if (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse)
            encoded = convertedCode;
        else if (dev == RE::INPUT_DEVICE::kGamepad)
            encoded = -(convertedCode + 2);
        if (encoded == -1) return false;
#ifdef DEBUG
        spdlog::info("[Input] TryHandleCapture: captured dev={} code={} encoded={}", static_cast<int>(dev),
                     convertedCode, encoded);
#endif
        cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
        cap.captureRequested.store(false, std::memory_order_relaxed);
        wantCapture = false;
        return true;
    }

    void UpdateDownState(RE::INPUT_DEVICE dev, int convertedCode, bool downNow) {
        if (dev == RE::INPUT_DEVICE::kKeyboard)
            g_kbDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
        else if (dev == RE::INPUT_DEVICE::kGamepad)
            g_gpDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
    }

    void DrainWhenBlocked() {
#ifdef DEBUG
        int drained = 0;
#endif
        for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot())
#ifdef DEBUG
            ++drained;
        if (drained > 0)
            spdlog::info("[Input] DrainWhenBlocked: discarded {} pressed slot(s) (input blocked)", drained);
#endif
        for (auto s2 = Input::ConsumeReleasedSlot(); s2.has_value(); s2 = Input::ConsumeReleasedSlot()) {
#ifdef DEBUG
            spdlog::info("[Input] DrainWhenBlocked: releasing slot={} while blocked", *s2);
#endif
            HandleSlotReleased(*s2);
        }
    }

    void DispatchSlots() {
        for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot())
            HandleSlotPressed(*s);
        for (auto s = Input::ConsumeReleasedSlot(); s.has_value(); s = Input::ConsumeReleasedSlot())
            HandleSlotReleased(*s);
    }

    int GamepadIdToIndex(int idCode) {
        using Key = RE::BSWin32GamepadDevice::Key;
        switch (static_cast<Key>(idCode)) {
            case Key::kUp:
                return 0;
            case Key::kDown:
                return 1;
            case Key::kLeft:
                return 2;
            case Key::kRight:
                return 3;
            case Key::kStart:
                return 4;
            case Key::kBack:
                return 5;
            case Key::kLeftThumb:
                return 6;
            case Key::kRightThumb:
                return 7;
            case Key::kLeftShoulder:
                return 8;
            case Key::kRightShoulder:
                return 9;
            case Key::kA:
                return 10;
            case Key::kB:
                return 11;
            case Key::kX:
                return 12;
            case Key::kY:
                return 13;
            case Key::kLeftTrigger:
                return 14;
            case Key::kRightTrigger:
                return 15;
            default:
                return -1;
        }
    }

    bool ShouldFilterAndSave(RE::INPUT_DEVICE dev, int convertedCode, std::uint32_t rawIdCode,
                             const RE::BSFixedString& userEvent, float value, float heldSecs) {
        const int effectiveKbCode =
            (dev == RE::INPUT_DEVICE::kMouse) ? (kMouseButtonBase + convertedCode) : convertedCode;

        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const auto& hk = g_cache[s];
            const bool inKb = (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse) &&
                              ComboContains(hk.kb, effectiveKbCode);
            const bool inGp = dev == RE::INPUT_DEVICE::kGamepad && ComboContains(hk.gp, convertedCode);
            if (!inKb && !inGp) continue;

            if (const bool accepted = g_slotDown[s].load(std::memory_order_relaxed); accepted) {
                return true;
            }

            if (g_slotWasAccepted[s]) {
#ifdef DEBUG
                spdlog::info("[Input] ShouldFilterAndSave: slot={} code={} dev={} FILTERED (wasAccepted)", slot,
                             effectiveKbCode, static_cast<int>(dev));
#endif
                return true;
            }

            if (inKb && ComboDown(hk.kb, g_kbDown)) {
                return true;
            }
            if (inGp && ComboDown(hk.gp, g_gpDown)) {
                return true;
            }

            if (ReplayMatchesEvent(s, dev, rawIdCode, userEvent, value)) {
#ifdef DEBUG
                spdlog::info("[Input] ShouldFilterAndSave: slot={} code={} replay PASS-THROUGH", slot, effectiveKbCode);
#endif
                ResetReplayState(s);
                return false;
            }

            if (g_slotIsMultiKey[s] && !HasExclusivePending(s)) {
                const bool simPatch = IntegratedMagic::GetMagicConfig().pressBothAtSamePatch && g_slotIsMultiKey[s];
                const bool replayInProgress = g_replay[s].armed || HasDeferredReplayForSlot(s);
                if ((simPatch && !g_simWindowActive[s]) || replayInProgress) {
                    continue;
                }
                bool sharedWithActiveSlot = false;
                for (int other = 0; other < n; ++other) {
                    if (other == slot) continue;
                    if (!g_slotDown[static_cast<std::size_t>(other)].load(std::memory_order_relaxed)) continue;
                    const auto& otherHk = g_cache[static_cast<std::size_t>(other)];
                    if (inKb && ComboContains(otherHk.kb, effectiveKbCode)) {
                        sharedWithActiveSlot = true;
                        break;
                    }
                    if (inGp && ComboContains(otherHk.gp, convertedCode)) {
                        sharedWithActiveSlot = true;
                        break;
                    }
                }
                if (sharedWithActiveSlot) return true;

#ifdef DEBUG
                spdlog::info(
                    "[Input] ShouldFilterAndSave: slot={} code={} starting exclusive pending (multiKey, no pending "
                    "yet)",
                    slot, effectiveKbCode);
#endif
                const PendingSrc src = inGp ? PendingSrc::Gp : PendingSrc::Kb;
                g_exclusivePendingSrc[s] = src;
                g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            }

            if (HasExclusivePending(s)) {
#ifdef DEBUG
                spdlog::info(
                    "[Input] ShouldFilterAndSave: slot={} code={} RETAINED (exclusive pending, value={:.2f}, "
                    "heldSecs={:.3f})",
                    slot, effectiveKbCode, value, heldSecs);
#endif
                g_retainedEvents[s].emplace_back(RetainedEvent{dev, rawIdCode, userEvent, value, heldSecs});
                return true;
            }
        }
        return false;
    }

    inline bool IsHudToggleCombo(RE::INPUT_DEVICE dev, int code) {
        if (dev == RE::INPUT_DEVICE::kKeyboard) return ComboContains(g_hudCache.kb, code);
        if (dev == RE::INPUT_DEVICE::kMouse) return ComboContains(g_hudCache.kb, kMouseButtonBase + code);
        if (dev == RE::INPUT_DEVICE::kGamepad) return ComboContains(g_hudCache.gp, code);
        return false;
    }

    bool HasTransformArchetype(const RE::MagicItem* item) {
        if (!item) return false;
        using ArchetypeID = RE::EffectArchetypes::ArchetypeID;
        return std::ranges::any_of(item->effects, [](const auto* effect) {
            if (!effect || !effect->baseEffect) return false;
            const auto arch = effect->baseEffect->GetArchetype();
            return arch == ArchetypeID::kWerewolf || arch == ArchetypeID::kVampireLord;
        });
    }

    bool IsTransformPowerEquipped(RE::PlayerCharacter* pc) {
        if (!pc) return false;
        const auto& rd = pc->GetActorRuntimeData();
        if (!rd.selectedPower) return false;
        return HasTransformArchetype(rd.selectedPower->As<RE::MagicItem>());
    }

    bool IsHudComboDown() { return ComboDown(g_hudCache.kb, g_kbDown) || ComboDown(g_hudCache.gp, g_gpDown); }

    bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode) {
        if (!IsHudToggleCombo(dev, convertedCode)) return false;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        return ui->IsMenuOpen(magicMenu);
    }

    void LoadHotkeyCache_FromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const auto n = static_cast<int>(cfg.SlotCount());
        g_slotCount.store(n, std::memory_order_relaxed);
        for (auto& s : g_cache) {
            s.kb = {-1, -1, -1};
            s.gp = {-1, -1, -1};
        }
        auto fill = [](SlotHotkeys& out, const auto& in) {
            out.kb[0] = in.KeyboardScanCode1.load(std::memory_order_relaxed);
            out.kb[1] = in.KeyboardScanCode2.load(std::memory_order_relaxed);
            out.kb[2] = in.KeyboardScanCode3.load(std::memory_order_relaxed);
            out.gp[0] = in.GamepadButton1.load(std::memory_order_relaxed);
            out.gp[1] = in.GamepadButton2.load(std::memory_order_relaxed);
            out.gp[2] = in.GamepadButton3.load(std::memory_order_relaxed);
        };
        const int m = std::min(n, kMaxSlots);
        for (int i = 0; i < m; ++i) {
            const auto s = static_cast<std::size_t>(i);
            fill(g_cache[s], cfg.slotInput[s]);
            const auto& hk = g_cache[s];
            const auto kbKeys = std::ranges::count_if(hk.kb, [](int c) { return c != -1; });
            const auto gpKeys = std::ranges::count_if(hk.gp, [](int c) { return c != -1; });
            g_slotIsMultiKey[s] = (kbKeys > 1) || (gpKeys > 1);
#ifdef DEBUG
            spdlog::info("[Input] LoadHotkeyCache: slot={} kb=[{},{},{}] gp=[{},{},{}] isMultiKey={}", i, hk.kb[0],
                         hk.kb[1], hk.kb[2], hk.gp[0], hk.gp[1], hk.gp[2], g_slotIsMultiKey[s]);
#endif
        }

        g_hudCache = {};
        fill(g_hudCache, cfg.hudPopupInput);
    }

    void ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn || (!btn->IsDown() && !btn->IsUp())) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->idCode);

            if (dev == RE::INPUT_DEVICE::kGamepad) {
                code = GamepadIdToIndex(code);
            }

            if (dev == RE::INPUT_DEVICE::kMouse) {
                const int mouseCode = kMouseButtonBase + code;
                if (mouseCode >= 0 && mouseCode < kMaxCode) {
                    (void)TryHandleCapture(btn, cap, wantCapture, RE::INPUT_DEVICE::kMouse, mouseCode);
                    g_kbDown[static_cast<std::size_t>(mouseCode)].store(btn->IsDown(), std::memory_order_relaxed);
                }
                continue;
            }

            if (code < 0 || code >= kMaxCode) continue;

            (void)TryHandleCapture(btn, cap, wantCapture, dev, code);
            UpdateDownState(dev, code, btn->IsDown());

            if (btn->IsDown() && player && btn->QUserEvent() == "Shout"sv) {
                if (IsTransformPowerEquipped(player)) {
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ProcessButtonEvents: Shout pressed with transform power equipped -> "
                        "ForceExitNoRestore");
#endif
                    IntegratedMagic::MagicState::Get().ForceExitNoRestore();
                }
            }
        }
    }

    void UpdateHudToggleState() {
        static bool prevHudDown = false;

        const bool hudDown = IsHudComboDown();
        if (hudDown && !prevHudDown) {
            auto* ui = RE::UI::GetSingleton();
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            if (ui && ui->IsMenuOpen(magicMenu)) {
                g_hudTogglePending.store(true, std::memory_order_relaxed);
            }
        }

        prevHudDown = hudDown;
    }

    void UpdateSlotsIfAllowed(bool blocked, float dt) {
        if (!blocked) {
            RecomputeSlotEdges(dt);
        } else {
            DrainWhenBlocked();
        }
    }

    void FilterMouseForPopup(RE::InputEvent** a_evns) {
        if (!IntegratedMagic::HUD::IsDetailPopupOpen()) return;

        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;

        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (cur->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                auto const* mm = static_cast<RE::MouseMoveEvent*>(cur);
                IntegratedMagic::HUD::FeedMouseDelta(static_cast<float>(mm->mouseInputX),
                                                     static_cast<float>(mm->mouseInputY));
                remove = true;

            } else if (cur->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto const* ts = static_cast<RE::ThumbstickEvent*>(cur);
                if (ts->IsLeft()) {
                    constexpr float kDeadzone = 0.15f;
                    constexpr float kSensitivity = 12.f;
                    const float ax = (std::abs(ts->xValue) > kDeadzone) ? ts->xValue : 0.f;
                    const float ay = (std::abs(ts->yValue) > kDeadzone) ? ts->yValue : 0.f;
                    if (ax != 0.f || ay != 0.f)
                        IntegratedMagic::HUD::FeedMouseDelta(ax * kSensitivity, -ay * kSensitivity);
                }

                remove = true;

            } else if (const auto* btn = cur->AsButtonEvent()) {
                if (btn->GetDevice() == RE::INPUT_DEVICE::kMouse && btn->GetIDCode() == 0) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseClick();
                    remove = true;
                } else if (btn->GetDevice() == RE::INPUT_DEVICE::kMouse && btn->GetIDCode() == 1) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseRightClick();
                    remove = true;
                } else if (btn->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
                           btn->GetIDCode() == static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kX)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseClick();
                    remove = true;
                } else if (btn->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
                           btn->GetIDCode() == static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kB)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseRightClick();
                    remove = true;
                } else if (btn->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
                           btn->GetIDCode() == static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Key::kY)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::CloseDetailPopup();
                    remove = true;
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    void FilterEvents(RE::InputEvent** a_evns) {
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;

        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (const auto* btn = cur->AsButtonEvent()) {
                const auto dev = btn->GetDevice();
                auto code = static_cast<int>(btn->idCode);
                const auto rawCode = btn->idCode;

                if (dev == RE::INPUT_DEVICE::kGamepad) {
                    code = GamepadIdToIndex(code);
                }

                if (code >= 0 && code < kMaxCode) {
                    remove =
                        ShouldFilterAndSave(dev, code, rawCode, btn->QUserEvent(), btn->Value(), btn->HeldDuration()) ||
                        ShouldFilterHudToggle(dev, code);

#ifdef DEBUG
                    if (!remove && (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kKeyboard)) {
                        const int effCode = (dev == RE::INPUT_DEVICE::kMouse) ? kMouseButtonBase + code : code;
                        const int n = ActiveSlots();
                        for (int slot = 0; slot < n; ++slot) {
                            if (ComboContains(g_cache[static_cast<std::size_t>(slot)].kb, effCode)) {
                                spdlog::info(
                                    "[Input] FilterEvents: slot={} code={} dev={} value={:.2f} PASSING TO ENGINE", slot,
                                    effCode, static_cast<int>(dev), btn->Value());
                                break;
                            }
                        }
                    }
#endif
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }

            cur = next;
        }
    }

    void DispatchIfAllowed(bool blocked, float dt) {
        if (!blocked) {
            DispatchSlots();
            IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
            IntegratedMagic::MagicState::Get().PumpAutomatic(dt);
        }
    }
}

void Input::ProcessAndFilter(RE::InputEvent** a_evns) {
    if (!a_evns) return;

    static bool s_cacheInitialized = false;
    if (!s_cacheInitialized) {
        LoadHotkeyCache_FromConfig();
        s_cacheInitialized = true;
    }

    DrainOneDeferredReplayEvent();

    for (int i = 0; i < ActiveSlots(); ++i) {
        const auto s = static_cast<std::size_t>(i);
        if (g_replay[s].armed && !HasDeferredReplayForSlot(s)) {
            ResetReplayState(s);
        }
    }

    for (int code = 0; code < kMouseButtonBase; ++code) {
        const auto idx = static_cast<std::size_t>(code);
        if (!g_kbDown[idx].load(std::memory_order_relaxed)) continue;
        const UINT vk = MapVirtualKeyA(static_cast<UINT>(code), MAPVK_VSC_TO_VK);
        if (vk == 0) continue;
        const bool physicallyDown = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
        if (!physicallyDown) {
#ifdef DEBUG
            spdlog::info("[Input] ProcessAndFilter: cleared stuck key scancode={}", code);
#endif
            g_kbDown[idx].store(false, std::memory_order_relaxed);
        }
    }

    static bool prevBlocked = false;
    auto& cap = GetCaptureState();
    bool wantCapture = cap.captureRequested.load(std::memory_order_relaxed);
    const bool wantCaptureBefore = wantCapture;

    const float dt = CalculateDeltaTime();
    const bool blocked = IsInputBlockedByMenus();

    if (prevBlocked && !blocked) {
#ifdef DEBUG
        spdlog::info("[Input] ProcessAndFilter: menu CLOSED - clearing stuck keys");
#endif
        ClearLikelyStuckKeysAfterMenuClose();
    }

    if (!prevBlocked && blocked) {
#ifdef DEBUG
        spdlog::info("[Input] ProcessAndFilter: menu OPENED - discarding all exclusive pending");
#endif
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) DiscardExclusivePending(static_cast<std::size_t>(slot));
    }

    prevBlocked = blocked;

    ProcessButtonEvents(a_evns, cap, wantCapture);
    UpdateHudToggleState();
    UpdateSlotsIfAllowed(blocked, dt);
    FilterMouseForPopup(a_evns);

    if (!blocked) {
        FilterEvents(a_evns);
    }

    if (wantCaptureBefore && !blocked) {
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            if (cur->AsButtonEvent()) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    DispatchIfAllowed(blocked, dt);
}

void Input::OnConfigChanged() {
#ifdef DEBUG
    spdlog::info("[Input] OnConfigChanged: reloading hotkey cache and resetting exclusive state");
#endif
    LoadHotkeyCache_FromConfig();
    ResetExclusiveState();
}

std::optional<int> Input::GetDownSlotForSelection() {
    const int n = ActiveSlots();
    for (int slot = 0; slot < n; ++slot)
        if (SlotComboDown(slot)) return slot;
    return std::nullopt;
}

bool Input::IsSlotHotkeyDown(int slot) { return SlotComboDown(slot); }

void Input::RequestHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(true, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int Input::PollCapturedHotkey() {
    auto& cap = GetCaptureState();
    if (const int v = cap.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        cap.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
    return -1;
}

std::optional<int> Input::ConsumePressedSlot() { return ConsumeBit(g_pressedMask); }
std::optional<int> Input::ConsumeReleasedSlot() { return ConsumeBit(g_releasedMask); }

bool Input::ConsumeHudToggle() { return g_hudTogglePending.exchange(false, std::memory_order_relaxed); }

bool Input::IsModifierHeld() {
    const auto& cfg = IntegratedMagic::GetMagicConfig();

    const int kbPos = cfg.modifierKeyboardPosition;
    const int gpPos = cfg.modifierGamepadPosition;

    if (kbPos > 0) {
        const auto& ic = cfg.slotInput[0];
        const int code = kbPos == 1   ? ic.KeyboardScanCode1.load(std::memory_order_relaxed)
                         : kbPos == 2 ? ic.KeyboardScanCode2.load(std::memory_order_relaxed)
                                      : ic.KeyboardScanCode3.load(std::memory_order_relaxed);
        if (code >= 0 && code < kMaxCode && g_kbDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed))
            return true;
    }
    if (gpPos > 0) {
        const auto& ic = cfg.slotInput[0];
        const int code = gpPos == 1   ? ic.GamepadButton1.load(std::memory_order_relaxed)
                         : gpPos == 2 ? ic.GamepadButton2.load(std::memory_order_relaxed)
                                      : ic.GamepadButton3.load(std::memory_order_relaxed);
        if (code >= 0 && code < kMaxCode && g_gpDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed))
            return true;
    }
    return false;
}

void Input::CancelHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(false, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

void Input::SetCaptureModeActive(bool active) { g_captureModeActive.store(active, std::memory_order_relaxed); }