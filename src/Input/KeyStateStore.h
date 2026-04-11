#pragma once
#include <array>
#include <atomic>

#include "Config/InputConstants.h"

namespace Input {

    struct KeyStateStore {
        std::array<std::atomic_bool, kMaxCode> kbDown{};
        std::array<std::atomic_bool, kMaxCode> gpDown{};

        [[nodiscard]] bool IsKbDown(int code) const noexcept {
            if (code < 0 || code >= kMaxCode) return false;
            return kbDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool IsGpDown(int code) const noexcept {
            if (code < 0 || code >= kMaxCode) return false;
            return gpDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        }

        void SetKbDown(int code, bool value) noexcept {
            if (code < 0 || code >= kMaxCode) return;
            kbDown[static_cast<std::size_t>(code)].store(value, std::memory_order_relaxed);
        }

        void SetGpDown(int code, bool value) noexcept {
            if (code < 0 || code >= kMaxCode) return;
            gpDown[static_cast<std::size_t>(code)].store(value, std::memory_order_relaxed);
        }
    };

}