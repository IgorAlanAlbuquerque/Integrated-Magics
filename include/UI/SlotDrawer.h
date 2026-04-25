#pragma once
#include <imgui.h>

#include "PCH.h"
#include "UI/HudView.h"

namespace IntegratedMagic::HUD::SlotDrawer {

    void DrawGlow(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol);
    void DrawSpellIcon(ImDrawList* dl, const RE::SpellItem* spell, float cx, float cy, float iconSize);

    void DrawSlotVisual(ImDrawList* dl, ImVec2 center, float r, bool isActive, RE::SpellItem const* rSpell,
                        RE::SpellItem const* lSpell, RE::FormID shoutFormID = 0, bool forceOffset = false,
                        bool canCast = true, bool onCooldown = false, float cooldownProgress = 0.0f);

    void DrawRingCenter(ImDrawList* dl, ImVec2 c, float r = 4.f);
    void DrawModifierWidget(ImDrawList* dl, ImVec2 c, bool modHeld, const HudView& v);
    void DrawSlotHotkeyIcons(ImDrawList* dl, ImVec2 center, float slotR, const SlotView& s);
    void DrawSlotButtonLabel(ImDrawList* dl, ImVec2 center, float slotR, const SlotView& s, const HudView& v,
                             ImVec2 hudOrigin, float alpha);

    void DrawSmallHUD(const ImGuiIO& io);
}