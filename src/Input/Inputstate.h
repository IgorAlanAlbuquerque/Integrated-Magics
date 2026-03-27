#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "Config/Config.h"
#include "PCH.h"

inline constexpr int kMaxSlots = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
inline constexpr int kMaxCode = 400;
inline constexpr int kMouseButtonBase = 256;
inline constexpr float kExclusiveConfirmDelaySec = 0.10f;

inline constexpr int kDIK_W = 0x11;
inline constexpr int kDIK_A = 0x1E;
inline constexpr int kDIK_S = 0x1F;
inline constexpr int kDIK_D = 0x20;
inline constexpr int kDIK_Escape = 0x01;

static_assert(kMaxSlots <= 64, "Input mask uses uint64_t, keep max slots <= 64.");

enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };

enum class ClearReason { Success, Timeout, Cancelled };

struct ReplayState {
    bool armed{false};
    bool skipNextSimWindowOpen{false};
    RE::INPUT_DEVICE dev{RE::INPUT_DEVICE::kKeyboard};
    std::uint32_t rawIdCode{0};
    RE::BSFixedString userEvent{};
    bool valueAboveHalf{false};
};

struct SlotHotkeys {
    std::array<int, 3> kb{-1, -1, -1};
    std::array<int, 3> gp{-1, -1, -1};
};

struct RetainedEvent {
    RE::INPUT_DEVICE dev;
    std::uint32_t rawIdCode;
    RE::BSFixedString userEvent;
    float value;
    float heldSecs;
};

struct DeferredReplayEvent {
    std::size_t slot{0};
    RetainedEvent ev{};
};

struct CaptureState {
    std::atomic_bool captureRequested{false};
    std::atomic_int capturedEncoded{-1};
};

extern std::array<std::atomic_bool, kMaxCode> g_kbDown;
extern std::array<std::atomic_bool, kMaxCode> g_gpDown;

extern std::atomic<int> g_slotCount;
extern std::array<std::atomic_bool, kMaxSlots> g_slotDown;
extern std::array<bool, kMaxSlots> g_slotWasAccepted;
extern std::array<bool, kMaxSlots> g_slotIsMultiKey;

extern std::atomic<std::uint64_t> g_pressedMask;
extern std::atomic<std::uint64_t> g_releasedMask;

extern std::array<PendingSrc, kMaxSlots> g_exclusivePendingSrc;
extern std::array<float, kMaxSlots> g_exclusivePendingTimer;
extern std::array<bool, kMaxSlots> g_slotFullComboSeen;
extern std::array<bool, kMaxSlots> g_prevRawKbDown;
extern std::array<bool, kMaxSlots> g_prevRawGpDown;

extern std::array<bool, kMaxSlots> g_prevAnyKeyDown;
extern std::array<bool, kMaxSlots> g_simWindowActive;
extern std::array<float, kMaxSlots> g_simWindowRemaining;

extern std::array<ReplayState, kMaxSlots> g_replay;
extern std::array<std::vector<RetainedEvent>, kMaxSlots> g_retainedEvents;
extern std::vector<DeferredReplayEvent> g_deferredEvents;

extern std::array<SlotHotkeys, kMaxSlots> g_cache;
extern SlotHotkeys g_hudCache;

extern std::atomic_bool g_hudTogglePending;
extern std::atomic_bool g_captureModeActive;

extern std::array<bool, kMaxSlots> g_slotIsKbMultiKey;
extern std::array<bool, kMaxSlots> g_slotIsGpMultiKey;

[[nodiscard]] inline int ActiveSlots() {
    int n = g_slotCount.load(std::memory_order_relaxed);
    if (n < 1) n = 1;
    if (n > kMaxSlots) n = kMaxSlots;
    return n;
}

[[nodiscard]] CaptureState& GetCaptureState();