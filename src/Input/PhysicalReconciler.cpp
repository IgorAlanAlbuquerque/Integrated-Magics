#include "PhysicalReconciler.h"

#include <chrono>

#include "Config/InputConstants.h"
#include "Input/ExclusiveTracker.h"
#include "PCH.h"
#include "State/State.h"

namespace Input::detail {

    void ReconcilePhysicalKeyState(KeyStateStore& keys, SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay,
                                   RetainedArr& retained, DeferredVec& deferred) {
        static std::uint64_t s_nextRunMs = 0;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (now < s_nextRunMs) return;
        s_nextRunMs = now + 200;

        if (IntegratedMagic::MagicState::Get().IsActive()) return;
        const int n = slots.ActiveSlots();
        for (int i = 0; i < n; ++i)
            if (HasExclusivePending(static_cast<std::size_t>(i), excl)) return;

        bool clearedAny = false;

        for (int code = 0; code < kMouseButtonBase; ++code) {
            const auto idx = static_cast<std::size_t>(code);
            if (!keys.kbDown[idx].load(std::memory_order_relaxed)) continue;
            const UINT vk = MapVirtualKeyA(static_cast<UINT>(code), MAPVK_VSC_TO_VK);
            if (vk == 0 || !(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)) {
                keys.kbDown[idx].store(false, std::memory_order_relaxed);
                clearedAny = true;
            }
        }

        static constexpr std::pair<int, int> kMouseVKMap[] = {
            {kMouseButtonBase + 0, VK_LBUTTON},  {kMouseButtonBase + 1, VK_RBUTTON},
            {kMouseButtonBase + 2, VK_MBUTTON},  {kMouseButtonBase + 3, VK_XBUTTON1},
            {kMouseButtonBase + 4, VK_XBUTTON2},
        };
        for (const auto& [idx, vk] : kMouseVKMap) {
            const auto i = static_cast<std::size_t>(idx);
            if (!keys.kbDown[i].load(std::memory_order_relaxed)) continue;
            if (!(GetAsyncKeyState(vk) & 0x8000)) {
                keys.kbDown[i].store(false, std::memory_order_relaxed);
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
            if (!keys.gpDown[i].load(std::memory_order_relaxed)) continue;
            if (!gamepadConnected || !(xstate.Gamepad.wButtons & mask)) {
                keys.gpDown[i].store(false, std::memory_order_relaxed);
                clearedAny = true;
            }
        }

        const auto iLT = static_cast<std::size_t>(14);
        if (keys.gpDown[iLT].load(std::memory_order_relaxed) &&
            !(gamepadConnected && xstate.Gamepad.bLeftTrigger > 64)) {
            keys.gpDown[iLT].store(false, std::memory_order_relaxed);
            clearedAny = true;
        }
        const auto iRT = static_cast<std::size_t>(15);
        if (keys.gpDown[iRT].load(std::memory_order_relaxed) &&
            !(gamepadConnected && xstate.Gamepad.bRightTrigger > 64)) {
            keys.gpDown[iRT].store(false, std::memory_order_relaxed);
            clearedAny = true;
        }

        if (clearedAny) {
#ifdef DEBUG
            spdlog::info("[Input] ReconcilePhysicalKeyState: cleared stuck keys");
#endif
            ClearEdgeStateOnly(slots, excl, replay, retained, deferred, keys);
        }
    }

}