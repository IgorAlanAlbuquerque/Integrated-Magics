#pragma once
#include <atomic>
#include <optional>

#include "Input/CaptureState.h"
#include "Input/ExclusiveStore.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Input/SlotEdgeStore.h"
#include "PCH.h"

namespace Application {

    class InputController {
    public:
        static InputController& Get();

        void ProcessAndFilter(RE::InputEvent** a_evns);
        void OnConfigChanged();

        [[nodiscard]] std::optional<int> ConsumePressedSlot();
        [[nodiscard]] std::optional<int> ConsumeReleasedSlot();

        [[nodiscard]] std::optional<int> GetDownSlotForSelection();
        [[nodiscard]] bool IsSlotHotkeyDown(int slot);
        [[nodiscard]] bool IsModifierHeld();
        [[nodiscard]] bool ConsumeHudToggle();

        void RequestHotkeyCapture();
        void CancelHotkeyCapture();
        [[nodiscard]] int PollCapturedHotkey();

        void SetCaptureModeActive(bool active);
        [[nodiscard]] bool IsCaptureModeActive();
        void InjectCapturedScancode(int scancode);
        void InjectCapturedGamepad(int buttonIndex);

        [[nodiscard]] Input::KeyStateStore& Keys() noexcept { return m_keys; }
        [[nodiscard]] Input::SlotEdgeStore& Slots() noexcept { return m_slots; }
        [[nodiscard]] Input::ExclusiveStore& Exclusive() noexcept { return m_exclusive; }
        [[nodiscard]] Input::HotkeyCacheStore& Hotkeys() noexcept { return m_hotkeys; }
        [[nodiscard]] CaptureState& Capture() noexcept { return m_captureState; }

        void SetSlotDeactivatedThisPress(int slot) noexcept {
            if (slot >= 0 && slot < kInputMaxSlots)
                m_exclusive.deactivatedThisPress[static_cast<std::size_t>(slot)] = true;
        }

    private:
        InputController() = default;

        CaptureState m_captureState{};
        bool m_captureModeActive{false};
        bool m_prevBlocked{false};
        bool m_cacheInitialized{false};
        Input::KeyStateStore m_keys{};
        Input::SlotEdgeStore m_slots{};
        Input::ExclusiveStore m_exclusive{};
        Input::HotkeyCacheStore m_hotkeys{};
        Input::detail::ReplayArr m_replay{};
        Input::detail::RetainedArr m_retained{};
        Input::detail::DeferredVec m_deferred{};

        void TryAssignHoveredToSlotByHotkey();
        static float CalculateDeltaTime();
        std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& mask);
    };

}