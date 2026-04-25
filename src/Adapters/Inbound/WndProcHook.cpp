#include "Adapters/Inbound/WndProcHook.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

#include "Adapters/Inbound/HookContext.h"
#include "Application/InputController.h"
#include "Config/InputConstants.h"
#include "PCH.h"
#include "UI/HudManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace IntegratedMagic::Inbound::WndProcHook {
    namespace {
        WNDPROC g_originalWndProc{nullptr};

        LRESULT Thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            if (HookContext::g_renderInitialized.load()) {
                ImGui::SetCurrentContext(HookContext::g_imguiContext);

                if (Application::InputController::Get().IsCaptureModeActive()) {
                    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
                        const UINT sc = (lParam >> 16) & 0x7F;
                        if (sc > 0 && sc < static_cast<UINT>(kMouseButtonBase))
                            Application::InputController::Get().InjectCapturedScancode(static_cast<int>(sc));
                    } else if (uMsg == WM_LBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 0);
                    } else if (uMsg == WM_RBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 1);
                    } else if (uMsg == WM_MBUTTONDOWN) {
                        Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + 2);
                    }
                }

                if (!Application::InputController::Get().IsCaptureModeActive()) {
                    if (uMsg == WM_KILLFOCUS) {
                        auto& io = ImGui::GetIO();
                        io.ClearInputCharacters();
                        io.ClearInputKeys();
                    }
                    const bool popupOpen = IntegratedMagic::HUD::IsDetailPopupOpen();
                    const bool isMouseMsg = (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN ||
                                             uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP ||
                                             uMsg == WM_MOUSEMOVE || uMsg == WM_MOUSEWHEEL);

                    if (!isMouseMsg || popupOpen) {
                        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
                    }
                }
            }
            return g_originalWndProc(hWnd, uMsg, wParam, lParam);
        }
    }

    void Install(HWND hwnd) {
        g_originalWndProc =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Thunk)));
        if (!g_originalWndProc) {
            spdlog::error("[Hooks] WndProcHook::Install: SetWindowLongPtrA failed");
        }
    }
}