#include "MagicInput.h"

#include <atomic>

#include "PCH.h"

namespace {
    struct CaptureState {
        std::atomic_bool captureRequested{false};
        std::atomic_int capturedEncoded{-1};
    };

    CaptureState& GetCaptureState() {
        static CaptureState st{};  // NOSONAR
        return st;
    }

    class IntegratedMagicInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static IntegratedMagicInputHandler* GetSingleton() {
            static IntegratedMagicInputHandler instance;
            return std::addressof(instance);
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
                                              RE::BSTEventSource<RE::InputEvent*>*) override {
            if (!a_events) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto& cap = GetCaptureState();
            if (!cap.captureRequested.load(std::memory_order_relaxed)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            for (auto e = *a_events; e; e = e->next) {
                auto const* btn = e->AsButtonEvent();
                if (!btn || !btn->IsDown()) {
                    continue;
                }

                const auto dev = btn->GetDevice();
                const auto code = static_cast<int>(btn->idCode);

                int encoded = -1;
                if (dev == RE::INPUT_DEVICE::kKeyboard) {
                    encoded = code;
                } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                    encoded = -(code + 1);
                } else {
                    continue;
                }

                cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
                cap.captureRequested.store(false, std::memory_order_relaxed);
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void MagicInput::RegisterInputHandler() {
    if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
        mgr->AddEventSink(IntegratedMagicInputHandler::GetSingleton());
    }
}

void MagicInput::RequestHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(true, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int MagicInput::PollCapturedHotkey() {
    auto& cap = GetCaptureState();

    if (int v = cap.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        cap.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }

    return -1;
}
