#pragma once
#include <cstdint>

namespace MagicInput {
    void RegisterInputHandler();
    void RequestHotkeyCapture();
    int PollCapturedHotkey();
}
