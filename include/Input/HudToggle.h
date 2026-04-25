#pragma once
#include <atomic>

#include "Config/InputConstants.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/HotkeyMatcher.h"
#include "Input/KeyStateStore.h"

extern std::atomic_bool g_hudTogglePending;  // NOSONAR

namespace Input::detail {

    [[nodiscard]] inline bool IsHudToggleCombo(RE::INPUT_DEVICE dev, int code, const HotkeyCacheStore& cache) {
        if (dev == RE::INPUT_DEVICE::kKeyboard) return ComboContains(cache.hud.kb, code);
        if (dev == RE::INPUT_DEVICE::kMouse) return ComboContains(cache.hud.kb, kMouseButtonBase + code);
        if (dev == RE::INPUT_DEVICE::kGamepad) return ComboContains(cache.hud.gp, code);
        return false;
    }

    [[nodiscard]] bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode, const HotkeyCacheStore& cache);

    void UpdateHudToggleState(const HotkeyCacheStore& cache, const KeyStateStore& keys);
}