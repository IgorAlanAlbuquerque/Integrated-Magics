#include "Application/InputController.h"

#include <xinput.h>

#include <chrono>
#include <utility>

#include "Adapters/Outbound/SyntheticInput.h"
#include "Domain/State.h"
#include "Input/ExclusiveTracker.h"
#include "Input/HotkeyMatcher.h"
#include "Input/HudToggle.h"
#include "Input/InputFilter.h"
#include "Input/PhysicalReconciler.h"
#include "Input/ReplaySystem.h"
#include "PCH.h"
#include "Shared/AssignService.h"
#include "Shared/Hand.h"

namespace Application {

    InputController& InputController::Get() {
        static InputController inst;  // NOSONAR
        return inst;
    }

    void InputController::ProcessAndFilter(RE::InputEvent** a_evns) {
        if (!a_evns) return;

        if (!m_cacheInitialized) {
            Input::detail::LoadHotkeyCache_FromConfig(m_hotkeys, m_slots);
            Input::detail::LoadModifierBinding_FromConfig(m_modifierKbCode, m_modifierGpCode);
            m_cacheInitialized = true;
        }

        if (Input::detail::ConsumeHotkeyReloadRequest()) {
            Input::detail::LoadHotkeyCache_FromConfig(m_hotkeys, m_slots);
            Input::detail::LoadModifierBinding_FromConfig(m_modifierKbCode, m_modifierGpCode);
            Input::detail::ResetExclusiveState(m_slots, m_exclusive, m_replay, m_retained, m_deferred);
        }

        for (int i = 0; i < m_slots.ActiveSlots(); ++i) {
            const auto s = static_cast<std::size_t>(i);
            if (m_replay[s].armed && !Input::detail::HasDeferredReplayForSlot(s, m_deferred))
                Input::detail::ResetReplayState(s, m_replay);
        }

        const bool spellSystemActive = IntegratedMagic::MagicState::Get().IsActive();
        const int activeSlot = IntegratedMagic::MagicState::Get().ActiveSlot();

        Input::detail::ReconcilePhysicalKeyState(m_keys, m_slots, m_exclusive, m_replay, m_retained, m_deferred,
                                                 spellSystemActive);

        bool wantCapture = CaptureState::Get().captureRequested.load(std::memory_order_relaxed);
        const bool wantCaptureBefore = wantCapture;
        const float dt = CalculateDeltaTime();
        m_lastDt = dt;
        const bool blocked = Input::detail::IsInputBlockedByMenus();

        if (m_prevBlocked && !blocked) {
            Input::detail::ClearLikelyStuckKeysAfterMenuClose(m_keys, m_slots, m_exclusive, m_hotkeys, m_replay,
                                                              m_retained, m_deferred);
        }
        if (!m_prevBlocked && blocked) {
            const int n = m_slots.ActiveSlots();
            for (int slot = 0; slot < n; ++slot)
                Input::detail::DiscardExclusivePending(static_cast<std::size_t>(slot), m_exclusive, m_replay,
                                                       m_retained, m_deferred);
        }
        m_prevBlocked = blocked;

        const auto buttonResult = Input::detail::ProcessButtonEvents(a_evns, CaptureState::Get(), wantCapture, m_keys);

        if (buttonResult.forceExit) {
            m_pendingForceExit = IntegratedMagic::MagicState::Get().ForceExitNoRestore();
        }
        Input::detail::UpdateHudToggleState(m_hotkeys, m_keys);

        Input::detail::UpdateSlotsIfAllowed(blocked, dt, m_slots, m_exclusive, m_hotkeys, m_keys, m_retained,
                                            m_deferred, m_replay, spellSystemActive, activeSlot);

        const auto replayResult = Input::detail::DrainOneDeferredReplayEvent(m_replay, m_deferred);
        if (replayResult.replayEvent) {
            const auto& ev = *replayResult.replayEvent;
            IntegratedMagic::detail::EnqueueRetainedEvent(ev.dev, ev.rawIdCode, ev.userEvent, ev.value, ev.heldSecs);
        }
        Input::detail::FilterMouseForPopup(a_evns);

        if (!blocked)
            Input::detail::FilterEvents(a_evns, m_keys, m_slots, m_hotkeys, m_exclusive, m_replay, m_retained);

        if (wantCaptureBefore && !blocked) {
            RE::InputEvent* prev = nullptr;
            RE::InputEvent* cur = *a_evns;
            while (cur) {
                RE::InputEvent* next = cur->next;
                if (cur->AsButtonEvent()) {
                    if (prev)
                        prev->next = next;
                    else
                        *a_evns = next;
                } else {
                    prev = cur;
                }
                cur = next;
            }
        }
    }

    void InputController::OnConfigChanged() {
        Input::detail::LoadHotkeyCache_FromConfig(m_hotkeys, m_slots);
        Input::detail::LoadModifierBinding_FromConfig(m_modifierKbCode, m_modifierGpCode);
        Input::detail::ResetExclusiveState(m_slots, m_exclusive, m_replay, m_retained, m_deferred);
    }

    std::optional<int> InputController::ConsumeBit(std::atomic<std::uint64_t>& mask) const {
        while (true) {
            const int n = m_slots.ActiveSlots();
            const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
            std::uint64_t curAll = mask.load(std::memory_order_relaxed);
            std::uint64_t cur = curAll & allowed;
            if (cur == 0uLL) {
                if (curAll != 0uLL)
                    (void)mask.compare_exchange_weak(curAll, curAll & allowed, std::memory_order_relaxed);
                return std::nullopt;
            }
            int idx = -1;
            for (int i = 0; i < n; ++i)
                if (cur & (1uLL << i)) {
                    idx = i;
                    break;
                }
            if (idx < 0) return std::nullopt;
            if (mask.compare_exchange_weak(curAll, curAll & ~(1uLL << idx), std::memory_order_relaxed)) return idx;
        }
    }

    std::optional<int> InputController::ConsumePressedSlot() { return ConsumeBit(m_slots.pressedMask); }
    std::optional<int> InputController::ConsumeReleasedSlot() { return ConsumeBit(m_slots.releasedMask); }

    std::optional<IntegratedMagic::StateExitResult> InputController::ConsumeForceExit() {
        return std::exchange(m_pendingForceExit, std::nullopt);
    }

    std::optional<int> InputController::GetDownSlotForSelection() const {
        const int n = m_slots.ActiveSlots();
        for (int slot = 0; slot < n; ++slot)
            if (Input::detail::SlotComboDown(slot, m_hotkeys, m_keys, m_slots)) return slot;
        return std::nullopt;
    }

    bool InputController::IsSlotHotkeyDown(int slot) const {
        return Input::detail::SlotComboDown(slot, m_hotkeys, m_keys, m_slots);
    }

    bool InputController::IsModifierHeld() {
        if (m_modifierKbCode >= 0 && m_modifierKbCode < kMaxCode &&
            m_keys.kbDown[static_cast<std::size_t>(m_modifierKbCode)].load(std::memory_order_relaxed))
            return true;
        if (m_modifierGpCode >= 0 && m_modifierGpCode < kMaxCode &&
            m_keys.gpDown[static_cast<std::size_t>(m_modifierGpCode)].load(std::memory_order_relaxed))
            return true;
        return false;
    }

    bool InputController::ConsumeHudToggle() const {
        return g_hudTogglePending.exchange(false, std::memory_order_relaxed);
    }

    void InputController::RequestHotkeyCapture() { CaptureState::Get().Request(); }
    void InputController::CancelHotkeyCapture() { CaptureState::Get().Cancel(); }
    int InputController::PollCapturedHotkey() { return CaptureState::Get().Poll(); }

    void InputController::SetCaptureModeActive(bool active) {
        CaptureState::Get().captureActive.store(active, std::memory_order_relaxed);
    }
    bool InputController::IsCaptureModeActive() const {
        return CaptureState::Get().captureActive.load(std::memory_order_relaxed);
    }

    void InputController::InjectCapturedScancode(int scancode) {
        auto& cap = CaptureState::Get();
        if (!cap.captureRequested.load(std::memory_order_relaxed)) return;
        cap.capturedEncoded.store(scancode, std::memory_order_relaxed);
        cap.captureRequested.store(false, std::memory_order_relaxed);
        cap.captureActive.store(false, std::memory_order_relaxed);
    }
    void InputController::InjectCapturedGamepad(int buttonIndex) {
        auto& cap = CaptureState::Get();
        if (!cap.captureRequested.load(std::memory_order_relaxed)) return;
        cap.capturedEncoded.store(-(buttonIndex + 2), std::memory_order_relaxed);
        cap.captureRequested.store(false, std::memory_order_relaxed);
        cap.captureActive.store(false, std::memory_order_relaxed);
    }

    float InputController::CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();
        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
        return dt;
    }

    bool InputController::IsInputBlocked() const { return Input::detail::IsInputBlockedByMenus(); }
}