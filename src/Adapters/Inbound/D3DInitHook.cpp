#include "Adapters/Inbound/D3DInitHook.h"

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Adapters/Inbound/HookContext.h"
#include "Adapters/Inbound/WndProcHook.h"
#include "Application/HudController.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace IntegratedMagic::Inbound::D3DInitHook {
    namespace {
        HWND FindGameWindow() {
            struct FindData {
                DWORD pid;
                HWND result;
            };
            FindData fd{GetCurrentProcessId(), nullptr};

            EnumWindows(
                [](HWND hWnd, LPARAM lParam) -> BOOL {
                    auto* fd = reinterpret_cast<FindData*>(lParam);
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hWnd, &pid);
                    if (pid != fd->pid || !IsWindowVisible(hWnd)) return TRUE;

                    char className[256]{};
                    GetClassNameA(hWnd, className, sizeof(className));
                    if (strcmp(className, "Skyrim Special Edition") != 0) return TRUE;

                    fd->result = hWnd;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&fd));

            return fd.result;
        }

        struct Impl {
            using FuncType = void (*)();
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75595, 77226, 0xDC5530);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x275, 0x9);

            static void thunk() {
                func();

                auto* rawDevice = RE::BSGraphics::Renderer::GetDevice();
                if (!rawDevice) {
                    spdlog::error("[Hooks] D3DInitHook: BSGraphics::Renderer::GetDevice() returned null");
                    return;
                }
                HookContext::g_device = reinterpret_cast<ID3D11Device*>(rawDevice);

                HookContext::g_device->GetImmediateContext(&HookContext::g_deviceContext);
                if (!HookContext::g_deviceContext) {
                    spdlog::error("[Hooks] D3DInitHook: GetImmediateContext failed");
                    return;
                }

                HWND hwnd = FindGameWindow();
                if (!hwnd) {
                    spdlog::error("[Hooks] D3DInitHook: could not find game window");
                    return;
                }

                HookContext::g_imguiContext = ImGui::CreateContext();
                ImGui::SetCurrentContext(HookContext::g_imguiContext);

                auto& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

                ImGui_ImplWin32_Init(hwnd);
                ImGui_ImplDX11_Init(HookContext::g_device, HookContext::g_deviceContext);

                Application::HudController::Get().InitializeGraphics();

                WndProcHook::Install(hwnd);

                HookContext::g_renderInitialized.store(true);
                MAGIC_DEBUG_LOG("[Hooks] D3DInitHook: ImGui HUD context initialized");
            }
        };
    }

    void Install() {
        Hook::stl::write_call<Impl>(Impl::id, Impl::offset);
        MAGIC_DEBUG_LOG("[Hooks] D3DInitHook installed");
    }
}