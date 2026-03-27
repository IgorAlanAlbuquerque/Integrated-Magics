#pragma once
#include <imgui.h>

#include <atomic>

namespace IntegratedMagic::HUD {
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
}