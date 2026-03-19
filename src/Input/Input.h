#pragma once
#include <cstdint>
#include <optional>

#include "RE/I/InputEvent.h"

namespace Input {
    void ProcessAndFilter(RE::InputEvent** a_evns);
    void OnConfigChanged();
    std::optional<int> GetDownSlotForSelection();
    bool IsSlotHotkeyDown(int slot);
    void RequestHotkeyCapture();
    void CancelHotkeyCapture();
    int PollCapturedHotkey();
    std::optional<int> ConsumePressedSlot();
    std::optional<int> ConsumeReleasedSlot();
    bool ConsumeHudToggle();
    bool IsModifierHeld();
}