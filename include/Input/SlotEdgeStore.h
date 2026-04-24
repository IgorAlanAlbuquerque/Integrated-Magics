#pragma once
#include <array>
#include <atomic>
#include <cstdint>

#include "Config/InputConstants.h"

namespace Input {

    struct SlotEdgeStore {
        std::atomic<int> slotCount{4};
        std::array<std::atomic_bool, kInputMaxSlots> slotDown{};
        std::array<bool, kInputMaxSlots> slotWasAccepted{};
        std::array<bool, kInputMaxSlots> slotIsMultiKey{};
        std::atomic<std::uint64_t> pressedMask{0uLL};
        std::atomic<std::uint64_t> releasedMask{0uLL};

        std::array<bool, kInputMaxSlots> slotIsKbMultiKey{};
        std::array<bool, kInputMaxSlots> slotIsGpMultiKey{};

        [[nodiscard]] int ActiveSlots() const noexcept {
            int n = slotCount.load(std::memory_order_relaxed);
            if (n < 1) n = 1;
            if (n > kInputMaxSlots) n = kInputMaxSlots;
            return n;
        }
    };

}