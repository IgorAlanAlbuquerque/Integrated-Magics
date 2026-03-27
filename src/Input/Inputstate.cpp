#include "InputState.h"

std::array<std::atomic_bool, kMaxCode> g_kbDown{};
std::array<std::atomic_bool, kMaxCode> g_gpDown{};

std::atomic<int> g_slotCount{4};
std::array<std::atomic_bool, kMaxSlots> g_slotDown{};
std::array<bool, kMaxSlots> g_slotWasAccepted{};
std::array<bool, kMaxSlots> g_slotIsMultiKey{};

std::atomic<std::uint64_t> g_pressedMask{0ull};
std::atomic<std::uint64_t> g_releasedMask{0ull};

std::array<PendingSrc, kMaxSlots> g_exclusivePendingSrc{};
std::array<float, kMaxSlots> g_exclusivePendingTimer{};
std::array<bool, kMaxSlots> g_slotFullComboSeen{};
std::array<bool, kMaxSlots> g_prevRawKbDown{};
std::array<bool, kMaxSlots> g_prevRawGpDown{};

std::array<bool, kMaxSlots> g_prevAnyKeyDown{};
std::array<bool, kMaxSlots> g_simWindowActive{};
std::array<float, kMaxSlots> g_simWindowRemaining{};

std::array<ReplayState, kMaxSlots> g_replay{};
std::array<std::vector<RetainedEvent>, kMaxSlots> g_retainedEvents{};
std::vector<DeferredReplayEvent> g_deferredEvents{};

std::array<SlotHotkeys, kMaxSlots> g_cache{};
SlotHotkeys g_hudCache{};

std::atomic_bool g_hudTogglePending{false};
std::atomic_bool g_captureModeActive{false};

std::array<bool, kMaxSlots> g_slotIsKbMultiKey{};
std::array<bool, kMaxSlots> g_slotIsGpMultiKey{};

CaptureState& GetCaptureState() {
    static CaptureState st{};
    return st;
}