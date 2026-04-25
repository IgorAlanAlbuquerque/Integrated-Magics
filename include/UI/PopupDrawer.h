#pragma once

#include <imgui.h>

namespace IntegratedMagic::HUD::PopupDrawer {

    void DrawOverlayAndCursor(ImVec2 displaySize, ImVec2 cursorPos);
    void DrawPopupActionHints(ImDrawList* dl, ImVec2 popupPos, ImVec2 popupSize, bool isShoutOrPower, bool hoverRight);
    void DrawSpellModeWidget(ImDrawList* dl, bool clicked, ImVec2 origin, float availW, std::uint32_t formID,
                             const char* id);
    void DrawDetailPopup();
}