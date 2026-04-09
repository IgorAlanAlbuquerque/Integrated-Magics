#include "Input.h"

#include <xinput.h>

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
#include "State/Assign.h"
#include "State/SpellClassify.h"
#include "State/State.h"
#include "UI/HoveredForm.h"
#include "UI/HudManager.h"

namespace {

    void TryAssignHoveredToSlotByHotkey() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        if (!ui->IsMenuOpen(magicMenu)) return;

        const auto type = IntegratedMagic::HoveredForm::GetHoveredMagicType();
        if (type == IntegratedMagic::HoveredForm::MagicType::None) return;

        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];
            const bool comboDown =
                Input::detail::ComboDown(hk.kb, g_kbDown) || Input::detail::ComboDown(hk.gp, g_gpDown);
            if (!comboDown) continue;

            using MT = IntegratedMagic::HoveredForm::MagicType;
            if (type == MT::Shout || type == MT::Power) {
                IntegratedMagic::MagicAssign::TryAssignHoveredShoutToSlot(slot);
            } else if (type == MT::RightOnlySpell) {
                IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(slot, IntegratedMagic::Slots::Hand::Right);
            } else {
                IntegratedMagic::MagicAssign::TryAssignHoveredSpellToSlot(slot, IntegratedMagic::Slots::Hand::Left);
            }
            break;
        }
    }

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

    void ReconcilePhysicalKeyState() {
        static std::uint64_t s_nextRunMs = 0;

        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (now < s_nextRunMs) return;
        s_nextRunMs = now + 150;

        if (IntegratedMagic::MagicState::Get().IsActive()) return;
        const int n = ActiveSlots();
        for (int i = 0; i < n; ++i) {
            if (Input::detail::HasExclusivePending(static_cast<std::size_t>(i))) return;
        }

        bool clearedAny = false;
        for (int code = 0; code < kMouseButtonBase; ++code) {
            const auto idx = static_cast<std::size_t>(code);
            if (!g_kbDown[idx].load(std::memory_order_relaxed)) continue;
            const UINT vk = MapVirtualKeyA(static_cast<UINT>(code), MAPVK_VSC_TO_VK);
            if (vk == 0 || !(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)) {
                g_kbDown[idx].store(false, std::memory_order_relaxed);
                clearedAny = true;
            }
        }

        for (const auto& [idx, vk] : kMouseVKMap) {
            const auto i = static_cast<std::size_t>(idx);
            if (!g_kbDown[i].load(std::memory_order_relaxed)) continue;
            if (!(GetAsyncKeyState(vk) & 0x8000)) {
                g_kbDown[i].store(false, std::memory_order_relaxed);
                clearedAny = true;
            }
        }

        XINPUT_STATE xstate{};
        const bool gamepadConnected = (XInputGetState(0, &xstate) == ERROR_SUCCESS);

        static constexpr std::pair<WORD, int> kGpMap[] = {
            {XINPUT_GAMEPAD_DPAD_UP, 0},
            {XINPUT_GAMEPAD_DPAD_DOWN, 1},
            {XINPUT_GAMEPAD_DPAD_LEFT, 2},
            {XINPUT_GAMEPAD_DPAD_RIGHT, 3},
            {XINPUT_GAMEPAD_START, 4},
            {XINPUT_GAMEPAD_BACK, 5},
            {XINPUT_GAMEPAD_LEFT_THUMB, 6},
            {XINPUT_GAMEPAD_RIGHT_THUMB, 7},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, 8},
            {XINPUT_GAMEPAD_RIGHT_SHOULDER, 9},
            {XINPUT_GAMEPAD_A, 10},
            {XINPUT_GAMEPAD_B, 11},
            {XINPUT_GAMEPAD_X, 12},
            {XINPUT_GAMEPAD_Y, 13},
        };

        for (const auto& [mask, gpIdx] : kGpMap) {
            const auto i = static_cast<std::size_t>(gpIdx);
            if (!g_gpDown[i].load(std::memory_order_relaxed)) continue;
            const bool physDown = gamepadConnected && (xstate.Gamepad.wButtons & mask);
            if (!physDown) {
                g_gpDown[i].store(false, std::memory_order_relaxed);
                clearedAny = true;
            }
        }

        {
            const auto iLT = static_cast<std::size_t>(14);
            if (g_gpDown[iLT].load(std::memory_order_relaxed)) {
                const bool physDown = gamepadConnected && (xstate.Gamepad.bLeftTrigger > 64);
                if (!physDown) {
                    g_gpDown[iLT].store(false, std::memory_order_relaxed);
                    clearedAny = true;
                }
            }
            const auto iRT = static_cast<std::size_t>(15);
            if (g_gpDown[iRT].load(std::memory_order_relaxed)) {
                const bool physDown = gamepadConnected && (xstate.Gamepad.bRightTrigger > 64);
                if (!physDown) {
                    g_gpDown[iRT].store(false, std::memory_order_relaxed);
                    clearedAny = true;
                }
            }
        }

        if (clearedAny) {
#ifdef DEBUG
            spdlog::info("[Input] ReconcilePhysicalKeyState: cleared stuck keys, resetting exclusive state");
#endif
            Input::detail::ClearEdgeStateOnly();
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

    for (int i = 0; i < ActiveSlots(); ++i) {
        const auto s = static_cast<std::size_t>(i);
        if (g_replay[s].armed && !Input::detail::HasDeferredReplayForSlot(s)) Input::detail::ResetReplayState(s);
    }

    ReconcilePhysicalKeyState();

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

    if (blocked) TryAssignHoveredToSlotByHotkey();

    Input::detail::UpdateSlotsIfAllowed(blocked, dt);
    Input::detail::DrainOneDeferredReplayEvent();
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