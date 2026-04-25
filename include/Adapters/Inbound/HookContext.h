#pragma once

#include <atomic>

struct ImGuiContext;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace IntegratedMagic::Inbound::HookContext {
    extern ImGuiContext* g_imguiContext;
    extern std::atomic<bool> g_renderInitialized;
    extern ID3D11Device* g_device;
    extern ID3D11DeviceContext* g_deviceContext;
}