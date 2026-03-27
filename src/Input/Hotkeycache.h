#pragma once

#include <algorithm>
#include <array>
#include <ranges>

#include "InputState.h"

namespace Input::detail {

    [[nodiscard]] inline bool AnyEnabled(const std::array<int, 3>& a) {
        return (a[0] != -1) || (a[1] != -1) || (a[2] != -1);
    }

    [[nodiscard]] inline bool ComboContains(const std::array<int, 3>& combo, int code) {
        return std::ranges::find(combo, code) != combo.end();
    }

    template <class DownArr>
    [[nodiscard]] bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) return false;
        return std::ranges::all_of(combo, [&](int code) {
            if (code == -1) return true;
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    template <class DownArr>
    [[nodiscard]] bool AnyComboKeyDown(const std::array<int, 3>& combo, const DownArr& down) {
        return std::ranges::any_of(combo, [&](int code) {
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    template <class DownArr, class AllowedFn>
    [[nodiscard]] bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down,
                                         AllowedFn isAllowedExtra) {
        if (!AnyEnabled(combo)) return false;
        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
            if (ComboContains(combo, code)) continue;
            if (isAllowedExtra(code)) continue;
            return false;
        }
        return true;
    }

    void LoadHotkeyCache_FromConfig();

    [[nodiscard]] bool SlotComboDown(int slot);

}