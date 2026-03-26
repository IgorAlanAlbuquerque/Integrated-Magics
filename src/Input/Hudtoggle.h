#pragma once

#include "HotkeyCache.h"
#include "InputState.h"

namespace Input::detail {

    [[nodiscard]] inline bool IsHudToggleCombo(RE::INPUT_DEVICE dev, int code) {
        if (dev == RE::INPUT_DEVICE::kKeyboard) return ComboContains(g_hudCache.kb, code);
        if (dev == RE::INPUT_DEVICE::kMouse) return ComboContains(g_hudCache.kb, kMouseButtonBase + code);
        if (dev == RE::INPUT_DEVICE::kGamepad) return ComboContains(g_hudCache.gp, code);
        return false;
    }

    [[nodiscard]] bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode);

    void UpdateHudToggleState();

}