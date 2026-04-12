#include "Hooks.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <xinput.h>

#include <utility>

#include "Adapters/Inbound/AnimEventAdapter.h"
#include "Adapters/Outbound/SyntheticInput.h"
#include "Application/InputController.h"
#include "Application/SpellSystemController.h"
#include "Config/InputConstants.h"
#include "HookUtil.hpp"
#include "PCH.h"
#include "Domain/State.h"
#include "UI/FontLoader.h"
#include "UI/HudManager.h"
#include "UI/HudState.h"
#include "UI/TextureManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace IntegratedMagic::Hooks {
    namespace {
        struct PollInputDevicesHook {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                if (!a_events) return;

                Application::InputController::Get().ProcessAndFilter(const_cast<RE::InputEvent**>(a_events));
                const float dt = Application::InputController::Get().GetDeltaTime();
                const bool blocked = Application::InputController::Get().IsInputBlocked();
                Application::SpellSystemController::Get().OnFrame(dt, blocked);
                RE::InputEvent* head = IntegratedMagic::detail::FlushSyntheticInput(*a_events);

                if (func == 0) return;
                RE::InputEvent* const arr[2]{head, nullptr};
                reinterpret_cast<Fn*>(func)(a_dispatcher, arr);
            }

            static void Install() {
                Hook::stl::write_call<PollInputDevicesHook>(REL::RelocationID(67315, 68617),
                                                            REL::VariantOffset(0x7B, 0x7B, 0x81));
            }
        };

        struct PlayerAnimGraphProcessEventHook {
            using Fn = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
                                                    const RE::BSAnimationGraphEvent*,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
            static inline Fn _orig{nullptr};

            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                  const RE::BSAnimationGraphEvent* a_ev,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
                const auto ret = _orig ? _orig(a_this, a_ev, a_src) : RE::BSEventNotifyControl::kContinue;
                if (a_ev) {
                    AnimListener::HandleAnimEvent(a_ev);
                }
                return ret;
            }

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};
                const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);
                _orig = reinterpret_cast<Fn>(orig);
            }
        };

        static ImGuiContext* g_imguiContext{nullptr};
        static std::atomic<bool> g_renderInitialized{false};
        static ID3D11Device* g_device{nullptr};
        static ID3D11DeviceContext* g_deviceContext{nullptr};

        struct WndProcHook {
            static inline WNDPROC func{nullptr};

            static LRESULT thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
                if (g_renderInitialized.load()) {
                    ImGui::SetCurrentContext(g_imguiContext);

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
                        const bool isMouseMsg =
                            (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN ||
                             uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP ||
                             uMsg == WM_MOUSEMOVE || uMsg == WM_MOUSEWHEEL);

                        if (!isMouseMsg || popupOpen) {
                            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
                        }
                    }
                }
                return func(hWnd, uMsg, wParam, lParam);
            }
        };

        int PollGamepadCapture() {
            static constexpr std::pair<WORD, int> kMap[] = {
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
            XINPUT_STATE state{};
            if (XInputGetState(0, &state) != ERROR_SUCCESS) return -1;
            for (const auto& [mask, idx] : kMap)
                if (state.Gamepad.wButtons & mask) return idx;

            if (state.Gamepad.bLeftTrigger > 64) return 14;
            if (state.Gamepad.bRightTrigger > 64) return 15;
            return -1;
        }

        struct D3DInitHook {
            using FuncType = void (*)();
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75595, 77226);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x275, 0x00);

            static void thunk() {
                func();

                auto* rawDevice = RE::BSGraphics::Renderer::GetDevice();
                if (!rawDevice) {
                    spdlog::error("[Hooks] D3DInitHook: BSGraphics::Renderer::GetDevice() returned null");
                    return;
                }
                g_device = reinterpret_cast<ID3D11Device*>(rawDevice);

                g_device->GetImmediateContext(&g_deviceContext);
                if (!g_deviceContext) {
                    spdlog::error("[Hooks] D3DInitHook: GetImmediateContext failed");
                    return;
                }

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

                HWND hwnd = fd.result;
                if (!hwnd) {
                    spdlog::error("[Hooks] D3DInitHook: could not find game window");
                    return;
                }

                g_imguiContext = ImGui::CreateContext();
                ImGui::SetCurrentContext(g_imguiContext);

                auto& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

                ImGui_ImplWin32_Init(hwnd);
                ImGui_ImplDX11_Init(g_device, g_deviceContext);

                IntegratedMagic::TextureManager::Init();

                const auto& fc = IntegratedMagic::StyleConfig::Get().font;
                if (const char* fontPath = fc.path.empty() ? nullptr : fc.path.c_str();
                    fontPath && std::filesystem::exists(fontPath)) {
                    io.Fonts->AddFontFromFileTTF(fontPath, fc.size, nullptr, FontLoader::GetGlyphRangesDefault());

                    ImFontConfig mergeCfg;
                    mergeCfg.MergeMode = true;

                    if (fc.rangePolish)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, FontLoader::GetGlyphRangesPolish());
                    if (fc.rangeCyrillic)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg,
                                                     FontLoader::GetGlyphRangesCyrillic());
                    if (fc.rangeJapanese)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg,
                                                     FontLoader::GetGlyphRangesJapanese());
                    if (fc.rangeChineseSimplified)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg,
                                                     FontLoader::GetGlyphRangesChineseSimplified());
                    if (fc.rangeKorean)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, FontLoader::GetGlyphRangesKorean());
                    if (fc.rangeGreek)
                        io.Fonts->AddFontFromFileTTF(fontPath, fc.size, &mergeCfg, FontLoader::GetGlyphRangesGreek());
                    MAGIC_DEBUG_LOG("[Hooks] D3DInitHook: loaded font '{}' size {}", fontPath, fc.size);
                } else {
                    io.Fonts->AddFontDefault();
                    MAGIC_DEBUG_LOG("[Hooks] D3DInitHook: font not found, using default");
                }

                WndProcHook::func = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));
                if (!WndProcHook::func) {
                    spdlog::error("[Hooks] D3DInitHook: SetWindowLongPtrA failed");
                }

                g_renderInitialized.store(true);
                MAGIC_DEBUG_LOG("[Hooks] D3DInitHook: ImGui HUD context initialized");
            }

            static void Install() {
                Hook::stl::write_call<D3DInitHook>(id, offset);
                MAGIC_DEBUG_LOG("[Hooks] D3DInitHook installed");
            }
        };

        struct DXGIPresentHook {
            using FuncType = void (*)(std::uint32_t);
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75461, 77246);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x9, 0x9);

            static inline float s_bbWidth = 0.f;
            static inline float s_bbHeight = 0.f;

            static void thunk(std::uint32_t a_p1) {
                func(a_p1);

                if (!g_renderInitialized.load()) return;

                ImGui::SetCurrentContext(g_imguiContext);

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();

                ID3D11RenderTargetView* rtv = nullptr;
                g_deviceContext->OMGetRenderTargets(1, &rtv, nullptr);
                if (rtv) {
                    ID3D11Resource* res = nullptr;
                    rtv->GetResource(&res);
                    if (res) {
                        ID3D11Texture2D* tex = nullptr;
                        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
                            D3D11_TEXTURE2D_DESC desc{};
                            tex->GetDesc(&desc);
                            IntegratedMagic::HUD::g_backbufferW.store(static_cast<float>(desc.Width),
                                                                      std::memory_order_relaxed);
                            IntegratedMagic::HUD::g_backbufferH.store(static_cast<float>(desc.Height),
                                                                      std::memory_order_relaxed);
                            tex->Release();
                        }
                        res->Release();
                    }
                    rtv->Release();
                } else {
                    spdlog::warn("[HUD] No RTV bound");
                }

                if (Application::InputController::Get().IsCaptureModeActive()) {
                    static bool s_prevMouse[5]{};
                    constexpr int kMouseVKs[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
                    for (int i = 0; i < 5; ++i) {
                        const bool down = (GetAsyncKeyState(kMouseVKs[i]) & 0x8000) != 0;
                        if (down && !s_prevMouse[i])
                            Application::InputController::Get().InjectCapturedScancode(kMouseButtonBase + i);
                        s_prevMouse[i] = down;
                    }

                    const int gpIdx = PollGamepadCapture();
                    if (gpIdx >= 0) Application::InputController::Get().InjectCapturedGamepad(gpIdx);
                }

                ImGui::NewFrame();
                IntegratedMagic::HUD::DrawHudFrame();
                ImGui::EndFrame();
                ImGui::Render();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            static void Install() {
                Hook::stl::write_call<DXGIPresentHook>(id, offset);
                MAGIC_DEBUG_LOG("[Hooks] DXGIPresentHook installed");
            }
        };
    }

    void Install_Hooks() {
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
        D3DInitHook::Install();
        DXGIPresentHook::Install();
    }
}