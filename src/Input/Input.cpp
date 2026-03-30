#include "Input.h"

#include <chrono>

#include "Input/EventFilter.h"
#include "Input/ExclusivePending.h"
#include "Input/HotkeyCache.h"
#include "Input/HudToggle.h"
#include "Input/InputInternal.h"
#include "Input/InputState.h"
#include "Input/ReplaySystem.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "State/State.h"
#include "UI/HudManager.h"

namespace {

    float CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();
        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
        return dt;
    }

    std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& maskAtomic) {
        while (true) {
            const int n = ActiveSlots();
            const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
            std::uint64_t curAll = maskAtomic.load(std::memory_order_relaxed);
            std::uint64_t cur = (curAll & allowed);
            if (cur == 0uLL) {
                if (curAll != 0uLL)
                    (void)maskAtomic.compare_exchange_weak(curAll, (curAll & allowed), std::memory_order_relaxed);
                return std::nullopt;
            }
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (cur & (1uLL << i)) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return std::nullopt;
            if (maskAtomic.compare_exchange_weak(curAll, (curAll & ~(1uLL << idx)), std::memory_order_relaxed))
                return idx;
        }
    }

    static constexpr std::pair<int, int> kMouseVKMap[] = {
        {kMouseButtonBase + 0, VK_LBUTTON},  {kMouseButtonBase + 1, VK_RBUTTON},  {kMouseButtonBase + 2, VK_MBUTTON},
        {kMouseButtonBase + 3, VK_XBUTTON1}, {kMouseButtonBase + 4, VK_XBUTTON2},
    };

    void ClearStuckKeysOnFocusRegain() {
        static bool s_prevFocused = true;

        const HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        const bool focused = (fgPid == GetCurrentProcessId());

        const bool justLostFocus = (s_prevFocused && !focused);
        const bool justRegainedFocus = (!s_prevFocused && focused);
        s_prevFocused = focused;

        if (justLostFocus) {
            for (const auto& [idx, vk] : kMouseVKMap) {
                const auto i = static_cast<std::size_t>(idx);
                if (g_kbDown[i].load(std::memory_order_relaxed)) {
#ifdef DEBUG
                    spdlog::info("[Input] ClearStuckKeysOnFocusRegain: focus lost, clearing mouse button idx={}", idx);
#endif
                    g_kbDown[i].store(false, std::memory_order_relaxed);
                }
            }
        }

        if (!justRegainedFocus) return;

#ifdef DEBUG
        spdlog::info("[Input] ClearStuckKeysOnFocusRegain: focus regained, checking for stuck keys");
#endif

        for (int code = 0; code < kMouseButtonBase; ++code) {
            const auto idx = static_cast<std::size_t>(code);
            if (!g_kbDown[idx].load(std::memory_order_relaxed)) continue;
            const UINT vk = MapVirtualKeyA(static_cast<UINT>(code), MAPVK_VSC_TO_VK);
            if (vk == 0) continue;
            if (!(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)) {
#ifdef DEBUG
                spdlog::info("[Input] ClearStuckKeysOnFocusRegain: cleared keyboard scancode={}", code);
#endif
                g_kbDown[idx].store(false, std::memory_order_relaxed);
            }
        }

        for (auto [idx, vk] : kMouseVKMap) {
            const auto i = static_cast<std::size_t>(idx);
            if (!g_kbDown[i].load(std::memory_order_relaxed)) continue;
            if (!(GetAsyncKeyState(vk) & 0x8000)) {
#ifdef DEBUG
                spdlog::info("[Input] ClearStuckKeysOnFocusRegain: cleared mouse button idx={}", idx);
#endif
                g_kbDown[i].store(false, std::memory_order_relaxed);
            }
        }
    }

}

std::optional<int> Input::ConsumePressedSlot() { return ConsumeBit(g_pressedMask); }
std::optional<int> Input::ConsumeReleasedSlot() { return ConsumeBit(g_releasedMask); }

void Input::ProcessAndFilter(RE::InputEvent** a_evns) {
    if (!a_evns) return;

    static bool s_cacheInitialized = false;
    if (!s_cacheInitialized) {
        Input::detail::LoadHotkeyCache_FromConfig();
        s_cacheInitialized = true;
    }

    Input::detail::DrainOneDeferredReplayEvent();

    ClearStuckKeysOnFocusRegain();

    static bool prevBlocked = false;
    auto& cap = GetCaptureState();
    bool wantCapture = cap.captureRequested.load(std::memory_order_relaxed);
    const bool wantCaptureBefore = wantCapture;

    const float dt = CalculateDeltaTime();
    const bool blocked = Input::detail::IsInputBlockedByMenus();

    if (prevBlocked && !blocked) {
#ifdef DEBUG
        spdlog::info("[Input] ProcessAndFilter: menu CLOSED - clearing stuck keys");
#endif
        Input::detail::ClearLikelyStuckKeysAfterMenuClose();
    }

    if (!prevBlocked && blocked) {
#ifdef DEBUG
        spdlog::info("[Input] ProcessAndFilter: menu OPENED - discarding all exclusive pending");
#endif
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) Input::detail::DiscardExclusivePending(static_cast<std::size_t>(slot));
    }

    prevBlocked = blocked;

    Input::detail::ProcessButtonEvents(a_evns, cap, wantCapture);
    Input::detail::UpdateHudToggleState();
    Input::detail::UpdateSlotsIfAllowed(blocked, dt);
    Input::detail::FilterMouseForPopup(a_evns);

    if (!blocked) Input::detail::FilterEvents(a_evns);

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

    for (int i = 0; i < ActiveSlots(); ++i) {
        const auto s = static_cast<std::size_t>(i);
        if (g_replay[s].armed && !Input::detail::HasDeferredReplayForSlot(s)) Input::detail::ResetReplayState(s);
    }

    Input::detail::DispatchIfAllowed(blocked, dt);
}

void Input::OnConfigChanged() {
#ifdef DEBUG
    spdlog::info("[Input] OnConfigChanged: reloading hotkey cache and resetting exclusive state");
#endif
    Input::detail::LoadHotkeyCache_FromConfig();
    Input::detail::ResetExclusiveState();
}

std::optional<int> Input::GetDownSlotForSelection() {
    const int n = ActiveSlots();
    for (int slot = 0; slot < n; ++slot)
        if (Input::detail::SlotComboDown(slot)) return slot;
    return std::nullopt;
}

bool Input::IsSlotHotkeyDown(int slot) { return Input::detail::SlotComboDown(slot); }

void Input::RequestHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(true, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

void Input::CancelHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(false, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int Input::PollCapturedHotkey() {
    auto& cap = GetCaptureState();
    if (const int v = cap.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        cap.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
    return -1;
}

bool Input::ConsumeHudToggle() { return g_hudTogglePending.exchange(false, std::memory_order_relaxed); }

bool Input::IsModifierHeld() {
    const auto& cfg = IntegratedMagic::GetMagicConfig();
    const int kbPos = cfg.modifierKeyboardPosition;
    const int gpPos = cfg.modifierGamepadPosition;

    if (kbPos > 0) {
        const auto& ic = cfg.slotInput[0];
        const int code = kbPos == 1   ? ic.KeyboardScanCode1.load(std::memory_order_relaxed)
                         : kbPos == 2 ? ic.KeyboardScanCode2.load(std::memory_order_relaxed)
                                      : ic.KeyboardScanCode3.load(std::memory_order_relaxed);
        if (code >= 0 && code < kMaxCode && g_kbDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed))
            return true;
    }
    if (gpPos > 0) {
        const auto& ic = cfg.slotInput[0];
        const int code = gpPos == 1   ? ic.GamepadButton1.load(std::memory_order_relaxed)
                         : gpPos == 2 ? ic.GamepadButton2.load(std::memory_order_relaxed)
                                      : ic.GamepadButton3.load(std::memory_order_relaxed);
        if (code >= 0 && code < kMaxCode && g_gpDown[static_cast<std::size_t>(code)].load(std::memory_order_relaxed))
            return true;
    }
    return false;
}

void Input::SetCaptureModeActive(bool active) { g_captureModeActive.store(active, std::memory_order_relaxed); }

bool Input::IsCaptureModeActive() { return g_captureModeActive.load(std::memory_order_relaxed); }

void Input::InjectCapturedScancode(int scancode) {
    auto& cap = GetCaptureState();
    if (!cap.captureRequested.load(std::memory_order_relaxed)) return;
#ifdef DEBUG
    spdlog::info("[Input] InjectCapturedScancode: scancode={}", scancode);
#endif
    cap.capturedEncoded.store(scancode, std::memory_order_relaxed);
    cap.captureRequested.store(false, std::memory_order_relaxed);
    g_captureModeActive.store(false, std::memory_order_relaxed);
}

void Input::InjectCapturedGamepad(int buttonIndex) {
    auto& cap = GetCaptureState();
    if (!cap.captureRequested.load(std::memory_order_relaxed)) return;
    const int encoded = -(buttonIndex + 2);
#ifdef DEBUG
    spdlog::info("[Input] InjectCapturedGamepad: index={} encoded={}", buttonIndex, encoded);
#endif
    cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
    cap.captureRequested.store(false, std::memory_order_relaxed);
    g_captureModeActive.store(false, std::memory_order_relaxed);
}