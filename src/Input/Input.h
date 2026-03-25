#pragma once
#include <cstdint>
#include <optional>

#include "RE/I/InputEvent.h"

constexpr int kMouseButtonBase = 256;

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
    void SetCaptureModeActive(bool active);
}