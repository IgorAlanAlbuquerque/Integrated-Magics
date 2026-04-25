#pragma once
#include <imgui.h>

#include <atomic>

#include "UI/HudView.h"

namespace IntegratedMagic::HUD {
    inline std::atomic<float> g_backbufferW{0.f};
    inline std::atomic<float> g_backbufferH{0.f};
    inline ImVec2 g_mousePos{0.f, 0.f};
    inline std::atomic_bool g_mouseClicked{false};
    inline std::atomic_bool g_mouseRightClicked{false};
    inline std::atomic_bool g_popupOpen{false};
    inline std::atomic_bool g_popupJustOpened{false};
    inline std::atomic_bool g_hudVisible{true};

    inline std::atomic_bool g_hardBlocked{false};
    inline std::atomic_bool g_softBlocked{false};
    inline std::atomic_bool g_inMagicMenu{false};
    inline std::atomic_bool g_hudShouldDraw{false};
    inline std::atomic_bool g_modifierHeld{false};
    inline std::atomic<int> g_slotCount{0};

    inline HudView g_hudView{};
    inline std::mutex g_hudViewMtx{};

    inline HudView SnapshotHudView() {
        std::scoped_lock _{g_hudViewMtx};
        return g_hudView;
    }
    inline void StoreHudView(const HudView& v) {
        std::scoped_lock _{g_hudViewMtx};
        g_hudView = v;
    }

    inline ImVec2 GetDisplaySize() {
        const float w = g_backbufferW.load(std::memory_order_relaxed);
        const float h = g_backbufferH.load(std::memory_order_relaxed);
        if (w > 0.f && h > 0.f) return {w, h};
        return ImGui::GetIO().DisplaySize;
    }
}