#pragma once
#include <atomic>

struct CaptureState {
    std::atomic_bool captureRequested{false};
    std::atomic_int capturedEncoded{-1};
};