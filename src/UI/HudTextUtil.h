#pragma once

#include <imgui.h>

#include <sstream>
#include <string>
#include <vector>

#include "UI/StyleConfig.h"

namespace IntegratedMagic::HUD {

    inline void DrawSpellLabel(const char* text, ImVec2 slotCenter, float slotRadius, ImVec2 dirTowardCenter,
                               ButtonLabelCorner position, float padding) {
        if (!text || text[0] == '\0') return;
        const auto& st = StyleConfig::Get();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!dl) return;

        ImVec2 dir{0.f, 1.f};
        switch (position) {
            case ButtonLabelCorner::Top:
                dir = {0.f, -1.f};
                break;
            case ButtonLabelCorner::Bottom:
                dir = {0.f, 1.f};
                break;
            case ButtonLabelCorner::Left:
                dir = {-1.f, 0.f};
                break;
            case ButtonLabelCorner::Right:
                dir = {1.f, 0.f};
                break;
            case ButtonLabelCorner::TowardCenter:
                dir = dirTowardCenter;
                break;
            case ButtonLabelCorner::AwayFromCenter:
                dir = {-dirTowardCenter.x, -dirTowardCenter.y};
                break;
        }

        const ImVec2 textSize = ImGui::CalcTextSize(text);

        const float anchorX = slotCenter.x + dir.x * (slotRadius + padding);
        const float anchorY = slotCenter.y + dir.y * (slotRadius + padding);

        float x = anchorX - textSize.x * 0.5f;
        float y = anchorY - textSize.y * 0.5f;

        if (dir.y < -0.1f)
            y = anchorY - textSize.y;
        else if (dir.y > 0.1f)
            y = anchorY;
        if (dir.x < -0.1f)
            x = anchorX - textSize.x;
        else if (dir.x > 0.1f)
            x = anchorX;

        if (st.textShadowEnabled) {
            dl->AddText({x + st.textShadowOffsetX, y + st.textShadowOffsetY}, st.textShadowColor, text);
        }
        dl->AddText({x, y}, st.textColor, text);
    }
}