#pragma once
#include <cstdint>

#include "UI/HudView.h"

namespace IntegratedMagic::HUD {
    void RefreshSlotCount();
    void EvaluateAndStoreHudVisibility(bool isSlotActive);
    void ExecutePopupIntents();
    void ExecuteHotkeyAssignment(std::uint64_t justPressedMask, int slotCount);
    void FillHudViewFromConfig(HudView& v);
}
