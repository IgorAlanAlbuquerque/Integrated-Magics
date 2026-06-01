#pragma once
#include <atomic>

struct CaptureState {
    std::atomic_bool captureRequested{false};
    std::atomic_int  capturedEncoded{-1};
    std::atomic_bool captureActive{false};

    static CaptureState& Get();

    void Request() noexcept {
        capturedEncoded.store(-1, std::memory_order_relaxed);
        captureRequested.store(true, std::memory_order_relaxed);
    }

    void Cancel() noexcept {
        captureRequested.store(false, std::memory_order_relaxed);
        capturedEncoded.store(-1, std::memory_order_relaxed);
    }

    [[nodiscard]] int Poll() noexcept {
        const int v = capturedEncoded.load(std::memory_order_relaxed);
        if (v == -1) return -1;
        capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
};
