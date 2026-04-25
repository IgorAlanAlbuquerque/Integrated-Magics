#include "Adapters/Inbound/HookContext.h"

namespace IntegratedMagic::Inbound::HookContext {
    ImGuiContext* g_imguiContext{nullptr};
    std::atomic<bool> g_renderInitialized{false};
    ID3D11Device* g_device{nullptr};
    ID3D11DeviceContext* g_deviceContext{nullptr};
}