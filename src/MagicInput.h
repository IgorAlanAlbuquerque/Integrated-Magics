#pragma once
#include <cstdint>
#include <optional>

namespace MagicInput {
    void RegisterInputHandler();
    void OnConfigChanged();
    std::optional<int> GetDownSlotForSelection();
    bool IsSlotHotkeyDown(int slot);
    void RequestHotkeyCapture();
    int PollCapturedHotkey();
    std::optional<int> ConsumePressedSlot();
    std::optional<int> ConsumeReleasedSlot();
    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
}
