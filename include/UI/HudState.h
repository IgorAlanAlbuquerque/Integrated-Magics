#pragma once
#include <imgui.h>

#include <atomic>

namespace IntegratedMagic::HUD {
    inline std::atomic<float> g_backbufferW{0.f};
    inline std::atomic<float> g_backbufferH{0.f};
    inline ImVec2 g_mousePos{0.f, 0.f};
    inline std::atomic_bool g_mouseClicked{false};
    inline std::atomic_bool g_mouseRightClicked{false};
    inline std::atomic_bool g_popupOpen{false};
    inline std::atomic_bool g_popupJustOpened{false};
    inline std::atomic_bool g_hudVisible{true};

    bool IsHardBlocked();
    bool IsSoftBlocked();
    bool IsInMagicMenu();
    bool EvaluateHudVisibility();
    inline ImVec2 GetDisplaySize() {
        const float w = g_backbufferW.load(std::memory_order_relaxed);
        const float h = g_backbufferH.load(std::memory_order_relaxed);
        if (w > 0.f && h > 0.f) return {w, h};
        return ImGui::GetIO().DisplaySize;
    }
}