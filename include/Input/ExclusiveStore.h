#pragma once
#include <array>
#include <cstdint>

#include "Config/InputConstants.h"

namespace Input {

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };
    enum class ClearReason { Success, Timeout, Cancelled };

    struct ExclusiveStore {
        std::array<PendingSrc, kInputMaxSlots> pendingSrc{};
        std::array<float, kInputMaxSlots> pendingTimer{};
        std::array<bool, kInputMaxSlots> fullComboSeen{};
        std::array<bool, kInputMaxSlots> prevRawKbDown{};
        std::array<bool, kInputMaxSlots> prevRawGpDown{};
        std::array<bool, kInputMaxSlots> prevAnyKeyDown{};
        std::array<bool, kInputMaxSlots> simWindowActive{};
        std::array<float, kInputMaxSlots> simWindowRemaining{};
        std::array<bool, kInputMaxSlots> isKbMultiKey{};
        std::array<bool, kInputMaxSlots> isGpMultiKey{};
        std::array<bool, kInputMaxSlots> filterWindowActive{};
        std::array<float, kInputMaxSlots> filterWindowTimer{};
        std::array<bool, kInputMaxSlots> deactivatedThisPress{};
    };

}