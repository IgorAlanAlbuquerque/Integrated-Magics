#pragma once

namespace IntegratedMagic::HUD {
    void Register();
    void ToggleDetailPopup();
    void CloseDetailPopup();

    // Usados por Input::ProcessAndFilter para filtrar eventos antes do Scaleform
    bool IsDetailPopupOpen();
    void FeedMouseDelta(float dx, float dy);
    void FeedMouseClick();
}