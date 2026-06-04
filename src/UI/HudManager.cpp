#include "UI/HudManager.h"

#include <imgui.h>

#include "PCH.h"
#include "UI/HudState.h"
#include "UI/PopupDrawer.h"
#include "UI/SlotDrawer.h"

namespace IntegratedMagic::HUD {

    void DrawHudFrame() {
        if (g_hardBlocked.load()) {
            if (g_popupOpen.load()) g_popupOpen.store(false);
            return;
        }
        if (g_slotCount.load() == 0) return;

        if (g_hudShouldDraw.load()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
            const bool showSmall = (!g_softBlocked.load() || g_inMagicMenu.load()) && !g_popupOpen.load();
            if (showSmall) SlotDrawer::DrawSmallHUD(ImGui::GetIO());
            ImGui::PopStyleVar(2);
        }

        if (g_popupOpen.load()) PopupDrawer::DrawDetailPopup();
    }

    bool IsDetailPopupOpen() { return g_popupOpen.load(std::memory_order_relaxed); }

    void FeedMouseDelta(float dx, float dy) {
        const ImGuiIO& io = ImGui::GetIO();
        g_mousePos.x = std::clamp(g_mousePos.x + dx, 0.f, io.DisplaySize.x);
        g_mousePos.y = std::clamp(g_mousePos.y + dy, 0.f, io.DisplaySize.y);
    }

    void FeedMouseClick() { g_mouseClicked.store(true, std::memory_order_relaxed); }
    void FeedMouseRightClick() { g_mouseRightClicked.store(true, std::memory_order_relaxed); }

    bool IsHudVisible() { return g_hudVisible.load(std::memory_order_relaxed); }
    void SetHudVisible(bool v) { g_hudVisible.store(v, std::memory_order_relaxed); }
}