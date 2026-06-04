#include "Adapters/Inbound/DXGIPresentHook.h"

#include <Windows.h>
#include <Xinput.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <utility>

#include "Adapters/Inbound/HookContext.h"
#include "Adapters/Inbound/HoveredForm.h"
#include "Application/HudController.h"
#include "Application/InputController.h"
#include "Shared/InputConstants.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace IntegratedMagic::Inbound::DXGIPresentHook {
    namespace {
        std::pair<float, float> QueryBackbufferSize() {
            ID3D11RenderTargetView* rtv = nullptr;
            HookContext::g_deviceContext->OMGetRenderTargets(1, &rtv, nullptr);
            if (!rtv) {
                spdlog::warn("[HUD] No RTV bound");
                return {0.f, 0.f};
            }

            float w = 0.f, h = 0.f;
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            if (res) {
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
                    D3D11_TEXTURE2D_DESC desc{};
                    tex->GetDesc(&desc);
                    w = static_cast<float>(desc.Width);
                    h = static_cast<float>(desc.Height);
                    tex->Release();
                }
                res->Release();
            }
            rtv->Release();
            return {w, h};
        }

        int PollGamepadCapture() {
            static constexpr std::pair<WORD, int> kMap[] = {
                {XINPUT_GAMEPAD_DPAD_UP, 0},      {XINPUT_GAMEPAD_DPAD_DOWN, 1},
                {XINPUT_GAMEPAD_DPAD_LEFT, 2},    {XINPUT_GAMEPAD_DPAD_RIGHT, 3},
                {XINPUT_GAMEPAD_START, 4},        {XINPUT_GAMEPAD_BACK, 5},
                {XINPUT_GAMEPAD_LEFT_THUMB, 6},   {XINPUT_GAMEPAD_RIGHT_THUMB, 7},
                {XINPUT_GAMEPAD_LEFT_SHOULDER, 8},{XINPUT_GAMEPAD_RIGHT_SHOULDER, 9},
                {XINPUT_GAMEPAD_A, 10},           {XINPUT_GAMEPAD_B, 11},
                {XINPUT_GAMEPAD_X, 12},           {XINPUT_GAMEPAD_Y, 13},
            };
            XINPUT_STATE state{};
            if (XInputGetState(0, &state) != ERROR_SUCCESS) return -1;
            for (const auto& [mask, idx] : kMap)
                if (state.Gamepad.wButtons & mask) return idx;

            if (state.Gamepad.bLeftTrigger > 64) return 14;
            if (state.Gamepad.bRightTrigger > 64) return 15;
            return -1;
        }

        void PollCapturedInput() {
            auto& input = Application::InputController::Get();
            if (!input.IsCaptureModeActive()) return;

            static bool s_prevMouse[5]{};
            constexpr int kMouseVKs[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            for (int i = 0; i < 5; ++i) {
                const bool down = (GetAsyncKeyState(kMouseVKs[i]) & 0x8000) != 0;
                if (down && !s_prevMouse[i]) input.InjectCapturedScancode(kMouseButtonBase + i);
                s_prevMouse[i] = down;
            }

            const int gpIdx = PollGamepadCapture();
            if (gpIdx >= 0) input.InjectCapturedGamepad(gpIdx);
        }

        struct Impl {
            using FuncType = void (*)(std::uint32_t);
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75461, 77246, 0xDBBDD0);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x9, 0x15);

            static void thunk(std::uint32_t a_p1) {
                func(a_p1);

                if (!HookContext::g_renderInitialized.load()) return;

                ImGui::SetCurrentContext(HookContext::g_imguiContext);
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();

                const auto [w, h] = QueryBackbufferSize();
                PollCapturedInput();
                HoveredForm::UpdateCachedState();
                Application::HudController::Get().RenderFrame(w, h);
            }
        };
    }

    void Install() {
        Hook::stl::write_call<Impl>(Impl::id, Impl::offset);
        MAGIC_DEBUG_LOG("[Hooks] DXGIPresentHook installed");
    }
}