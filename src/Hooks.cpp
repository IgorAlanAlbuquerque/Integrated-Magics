#include "Hooks.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <utility>

#include "HookUtil.hpp"
#include "Input/Input.h"
#include "PCH.h"
#include "State/AnimListener.h"
#include "State/State.h"
#include "UI/FontLoader.h"
#include "UI/HudManager.h"
#include "UI/TextureManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace IntegratedMagic::Hooks {
    namespace {
        struct PollInputDevicesHook {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                if (!a_events) return;

                Input::ProcessAndFilter(const_cast<RE::InputEvent**>(a_events));
                if (func == 0) return;
                RE::InputEvent* const arr[2]{*a_events, nullptr};
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
                    AnimListener::HandleAnimEvent(a_ev, a_src);
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
                    if (uMsg == WM_KILLFOCUS) {
                        auto& io = ImGui::GetIO();
                        io.ClearInputCharacters();
                        io.ClearInputKeys();
                    }
                    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
                }
                return func(hWnd, uMsg, wParam, lParam);
            }
        };

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

                HWND hwnd = FindWindowA("Skyrim Special Edition", nullptr);
                if (!hwnd) {
                    spdlog::error("[Hooks] D3DInitHook: FindWindowA failed");
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
                const char* fontPath = fc.path.empty() ? nullptr : fc.path.c_str();
                if (fontPath && std::filesystem::exists(fontPath)) {
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

                    spdlog::info("[Hooks] D3DInitHook: loaded font '{}' size {}", fontPath, fc.size);
                } else {
                    io.Fonts->AddFontDefault();
                    if (fontPath) spdlog::warn("[Hooks] D3DInitHook: font not found at '{}', using default", fontPath);
                }

                WndProcHook::func = reinterpret_cast<WNDPROC>(
                    SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));
                if (!WndProcHook::func) {
                    spdlog::error("[Hooks] D3DInitHook: SetWindowLongPtrA failed");
                }

                g_renderInitialized.store(true);
                spdlog::info("[Hooks] D3DInitHook: ImGui HUD context initialized");
            }

            static void Install() {
                Hook::stl::write_call<D3DInitHook>(id, offset);
                spdlog::info("[Hooks] D3DInitHook installed");
            }
        };

        struct DXGIPresentHook {
            using FuncType = void (*)(std::uint32_t);
            static inline REL::Relocation<FuncType> func;
            static constexpr auto id = REL::RelocationID(75461, 77246);
            static constexpr auto offset = REL::VariantOffset(0x9, 0x9, 0x9);

            static void thunk(std::uint32_t a_p1) {
                func(a_p1);

                if (!g_renderInitialized.load()) return;

                ImGui::SetCurrentContext(g_imguiContext);

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                IntegratedMagic::HUD::DrawHudFrame();

                ImGui::EndFrame();
                ImGui::Render();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            static void Install() {
                Hook::stl::write_call<DXGIPresentHook>(id, offset);
                spdlog::info("[Hooks] DXGIPresentHook installed");
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