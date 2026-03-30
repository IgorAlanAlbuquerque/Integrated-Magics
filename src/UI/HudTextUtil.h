#pragma once

#include <imgui.h>

#include <sstream>
#include <string>
#include <vector>

namespace IntegratedMagic::HUD {

    inline void DrawWrappedLabelAbove(const char* text, float columnLeftX, float maxWidth, float slotTopY,
                                      float margin = 4.f, bool centered = false) {
        if (!text || text[0] == '\0') return;
        const float lineHeight = ImGui::GetTextLineHeight();
        std::vector<std::string> lines;
        std::string current;
        std::istringstream stream(text);
        std::string word;
        while (stream >> word) {
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                current = candidate;
            else {
                if (!current.empty()) lines.push_back(current);
                current = word;
            }
        }
        if (!current.empty()) lines.push_back(current);
        float y = slotTopY - margin - static_cast<float>(lines.size()) * lineHeight;
        for (const auto& line : lines) {
            float x = columnLeftX;
            if (centered) {
                const float lineW = ImGui::CalcTextSize(line.c_str()).x;
                x = columnLeftX + (maxWidth - lineW) * 0.5f;
            }
            ImGui::SetCursorScreenPos({x, y});
            ImGui::TextDisabled("%s", line.c_str());
            y += lineHeight;
        }
    }
}