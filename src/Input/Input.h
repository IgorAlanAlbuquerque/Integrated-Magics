#pragma once

#include <optional>

#include "PCH.h"

namespace Input {
    void ProcessAndFilter(RE::InputEvent** a_evns);
    void OnConfigChanged();
    [[nodiscard]] std::optional<int> GetDownSlotForSelection();
    [[nodiscard]] bool IsSlotHotkeyDown(int slot);
    void RequestHotkeyCapture();
    void CancelHotkeyCapture();
    [[nodiscard]] int PollCapturedHotkey();
    [[nodiscard]] bool ConsumeHudToggle();
    [[nodiscard]] bool IsModifierHeld();
    void SetCaptureModeActive(bool active);
    [[nodiscard]] bool IsCaptureModeActive();
    void InjectCapturedScancode(int scancode);
    void InjectCapturedGamepad(int buttonIndex);
}