#pragma once

namespace IntegratedMagic::HUD {
    void DrawHudFrame();

    void ToggleDetailPopup();
    void CloseDetailPopup();
    bool IsDetailPopupOpen();

    void FeedMouseDelta(float dx, float dy);
    void FeedMouseClick();
    void FeedMouseRightClick();

    bool IsHudVisible();
    void SetHudVisible(bool visible);
}