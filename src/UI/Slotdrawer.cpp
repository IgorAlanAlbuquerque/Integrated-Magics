#include "SlotDrawer.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <numbers>

#include "Config/Config.h"
#include "Config/Slots.h"
#include "Input/Input.h"
#include "PCH.h"
#include "State/SpellClassify.h"
#include "State/State.h"
#include "UI/HudState.h"
#include "UI/HudTextUtil.h"
#include "UI/PolyFill.h"
#include "UI/SlotAnimator.h"
#include "UI/SlotLayout.h"
#include "UI/StyleConfig.h"
#include "UI/TextureManager.h"

namespace IntegratedMagic::HUD::SlotDrawer {

    namespace {
        constexpr float kPI = std::numbers::pi_v<float>;
        constexpr float kGlowPad = 16.f;

        constexpr const char* kHudWindowID = "##IMAGIC_HUD";

        inline const StyleConfig& Style() { return StyleConfig::Get(); }

        struct Palette {
            ImU32 fill;
            ImU32 glow;
        };

        Palette SchoolPalette(RE::ActorValue av) {
            const auto& st = Style();
            switch (av) {
                using enum RE::ActorValue;
                case kAlteration:
                    return {st.alterationFill, st.alterationGlow};
                case kConjuration:
                    return {st.conjurationFill, st.conjurationGlow};
                case kDestruction:
                    return {st.destructionFill, st.destructionGlow};
                case kIllusion:
                    return {st.illusionFill, st.illusionGlow};
                case kRestoration:
                    return {st.restorationFill, st.restorationGlow};
                default:
                    return {st.defaultFill, st.defaultGlow};
            }
        }

        Palette SpellPalette(RE::SpellItem const* spell) {
            if (!spell) return {Style().emptyFill, IM_COL32(0, 0, 0, 0)};
            const auto* fx = spell->GetCostliestEffectItem();
            if (!fx || !fx->baseEffect) return SchoolPalette(RE::ActorValue::kNone);
            auto av = fx->baseEffect->GetMagickSkill();
            if (av == RE::ActorValue::kNone) av = fx->baseEffect->data.primaryAV;
            return SchoolPalette(av);
        }

        inline float DynamicRingRadius(int n, float slotR, float baseR, float gap = 8.f) {
            if (n <= 1) return baseR;
            const float minR = (slotR + gap * 0.5f) / std::sin(kPI / static_cast<float>(n));
            return std::max(baseR, minR);
        }

        ImVec2 ComputeHudCenter(const ImGuiIO& io, ImVec2 halfSize) {
            const float W = io.DisplaySize.x;
            const float H = io.DisplaySize.y;
            const auto& st = Style();
            const float mx = halfSize.x + 4.f;
            const float my = halfSize.y + 4.f;
            const float ox = st.hudOffsetX;
            const float oy = st.hudOffsetY;

            switch (st.hudAnchor) {
                using enum HudAnchor;
                case TopLeft:
                    return {mx + ox, my + oy};
                case TopCenter:
                    return {W * 0.5f + ox, my + oy};
                case TopRight:
                    return {W - mx + ox, my + oy};
                case MiddleLeft:
                    return {mx + ox, H * 0.5f + oy};
                case Center:
                    return {W * 0.5f + ox, H * 0.5f + oy};
                case MiddleRight:
                    return {W - mx + ox, H * 0.5f + oy};
                case BottomLeft:
                    return {mx + ox, H - my + oy};
                case BottomCenter:
                    return {W * 0.5f + ox, H - my + oy};
                case BottomRight:
                default:
                    return {W - mx + ox, H - my + oy};
            }
        }

        const TextureManager::Image& ResolveModifierIcon() {
            static const TextureManager::Image kEmpty{};
            const auto& cfg = IntegratedMagic::GetMagicConfig();
            const auto& st = StyleConfig::Get();

            if (st.buttonIconType == ButtonIconType::Keyboard) {
                const int kbPos = cfg.modifierKeyboardPosition;
                if (kbPos <= 0) return kEmpty;
                const auto& ic = cfg.slotInput[0];
                int sc = kbPos == 1   ? ic.KeyboardScanCode1.load(std::memory_order_relaxed)
                         : kbPos == 2 ? ic.KeyboardScanCode2.load(std::memory_order_relaxed)
                                      : ic.KeyboardScanCode3.load(std::memory_order_relaxed);
                if (sc < 0) return kEmpty;
                return TextureManager::GetKeyboardIcon(sc);
            } else {
                const int gpPos = cfg.modifierGamepadPosition;
                if (gpPos <= 0) return kEmpty;
                const auto& ic = cfg.slotInput[0];
                int idx = gpPos == 1   ? ic.GamepadButton1.load(std::memory_order_relaxed)
                          : gpPos == 2 ? ic.GamepadButton2.load(std::memory_order_relaxed)
                                       : ic.GamepadButton3.load(std::memory_order_relaxed);
                if (idx < 0) return kEmpty;
                return TextureManager::GetGamepadButtonIcon(idx, st.buttonIconType);
            }
        }
    }

    void PathSlotShape(ImDrawList* dl, ImVec2 center, float r) {
        const auto& shape = StyleConfig::Get().slotShape;
        if (shape.vertices.size() >= 3) {
            for (const auto& v : shape.vertices) dl->PathLineTo({center.x + v.x * r, center.y + v.y * r});
        } else {
            dl->PathArcTo(center, r, 0.f, 2.f * kPI, 48);
        }
    }

    void FillSlotShape(ImDrawList* dl, ImVec2 center, float r, ImU32 col) {
        const auto& shape = StyleConfig::Get().slotShape;
        if (shape.vertices.size() >= 3) {
            if (PolyFill::IsConvex(shape.vertices)) {
                for (const auto& v : shape.vertices) dl->PathLineTo({center.x + v.x * r, center.y + v.y * r});
                dl->PathFillConvex(col);
            } else {
                for (const auto& t : PolyFill::Triangulate(shape.vertices, center.x, center.y, r))
                    dl->AddTriangleFilled({t.ax, t.ay}, {t.bx, t.by}, {t.cx, t.cy}, col);
            }
        } else {
            dl->AddCircleFilled(center, r, col, 48);
        }
    }

    void StrokeSlotShape(ImDrawList* dl, ImVec2 center, float r, ImU32 col, float thickness) {
        PathSlotShape(dl, center, r);
        dl->PathStroke(col, ImDrawFlags_Closed, thickness);
    }

    void DrawGlowShape(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol) {
        const auto& shape = StyleConfig::Get().slotShape;
        const ImU32 base = glowCol & 0x00FFFFFFu;
        const auto baseA = static_cast<int>((glowCol >> 24) & 0xFF);
        if (shape.useCustomShape && shape.vertices.size() >= 3) {
            for (int i = 5; i >= 1; --i) {
                const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
                StrokeSlotShape(dl, c, r + static_cast<float>(i) * 2.5f, layer, 1.2f);
            }
        } else {
            for (int i = 5; i >= 1; --i) {
                const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
                dl->AddCircle(c, r + static_cast<float>(i) * 2.5f, layer, 48, 1.2f);
            }
        }
    }

    void DrawGlow(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol) {
        const ImU32 base = glowCol & 0x00FFFFFFu;
        const auto baseA = static_cast<int>((glowCol >> 24) & 0xFF);
        for (int i = 5; i >= 1; --i) {
            const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
            dl->AddCircle(c, r + static_cast<float>(i) * 2.5f, layer, 48, 1.2f);
        }
    }

    void DrawSpellIcon(ImDrawList* dl, const RE::SpellItem* spell, float cx, float cy, float iconSize) {
        const auto& img = TextureManager::GetSpellIcon(spell);
        if (!img.valid()) return;
        const float half = iconSize * 0.5f;
        dl->AddImage(reinterpret_cast<ImTextureID>(img.texture), {cx - half, cy - half}, {cx + half, cy + half},
                     {0.f, 0.f}, {1.f, 1.f}, IM_COL32(255, 255, 255, Style().iconAlpha));
    }

    void DrawSlotVisual(ImDrawList* dl, ImVec2 center, float r, bool isActive, RE::SpellItem const* rSpell,
                        RE::SpellItem const* lSpell, RE::FormID shoutFormID, bool forceOffset) {
        const auto rPal = SpellPalette(rSpell);
        const auto lPal = SpellPalette(lSpell);

        if (isActive) {
            DrawGlowShape(dl, center, r, rPal.glow);
            DrawGlowShape(dl, center, r, lPal.glow);
        }

        const auto& st = Style();

        if (st.useTextureForSlotBg) {
            const bool isEmpty = (!rSpell && !lSpell && !shoutFormID);
            const auto& bgImg = [&]() -> const TextureManager::Image& {
                if (isEmpty) {
                    const auto& e = TextureManager::GetUiTexture(UiTextureType::slot_bg_empty);
                    if (e.valid()) return e;
                } else if (isActive) {
                    const auto& a = TextureManager::GetUiTexture(UiTextureType::slot_bg_active);
                    if (a.valid()) return a;
                }
                return TextureManager::GetUiTexture(UiTextureType::slot_bg);
            }();
            if (bgImg.valid())

                dl->AddImage(reinterpret_cast<ImTextureID>(bgImg.texture), {center.x - r, center.y - r},
                             {center.x + r, center.y + r}, {0.f, 0.f}, {1.f, 1.f}, IM_COL32(255, 255, 255, 255));
            else
                FillSlotShape(dl, center, r, isActive ? st.slotBgActive : st.slotBgInactive);
        } else {
            FillSlotShape(dl, center, r, isActive ? st.slotBgActive : st.slotBgInactive);
        }

        const float iconSize = r * st.iconSizeFactor;
        if (shoutFormID) {
            const auto& img = TextureManager::GetIconForForm(shoutFormID);
            if (img.valid()) {
                const float half = iconSize * 0.6f;
                dl->AddImage(reinterpret_cast<ImTextureID>(img.texture), {center.x - half, center.y - half},
                             {center.x + half, center.y + half}, {0.f, 0.f}, {1.f, 1.f},
                             IM_COL32(255, 255, 255, st.iconAlpha));
            }
        } else {
            const float off = r * st.iconOffsetFactor;
            const bool sameSpell = rSpell && lSpell && (rSpell->GetFormID() == lSpell->GetFormID());
            const bool onlyOne = (rSpell != nullptr) != (lSpell != nullptr);

            if (!forceOffset && (sameSpell || onlyOne)) {
                const RE::SpellItem* sp = rSpell ? rSpell : lSpell;
                DrawSpellIcon(dl, sp, center.x, center.y, iconSize);
            } else {
                if (rSpell) DrawSpellIcon(dl, rSpell, center.x + off, center.y, iconSize);
                if (lSpell) DrawSpellIcon(dl, lSpell, center.x - off, center.y, iconSize);
            }
        }

        if (isActive) {
            const double t = ImGui::GetTime();
            const double pulse = 0.65 + 0.35 * std::sin(t * 4.5);
            const ImU32 ring =
                IM_COL32(255, static_cast<int>(210 * pulse), static_cast<int>(50 * pulse), st.slotRingActiveAlpha);
            StrokeSlotShape(dl, center, r, ring, st.slotRingWidth);
            StrokeSlotShape(dl, center, r + st.slotRingWidth + 0.5f, (ring & 0x00FFFFFFu) | (70u << 24), 1.0f);
        } else {
            StrokeSlotShape(dl, center, r, st.slotRingInactive, st.slotRingWidth * 0.5f);
        }

        if (!rSpell && !lSpell && !shoutFormID) {
            const float d = r * 0.32f;
            const ImU32 xc = st.emptySlotColor;
            dl->AddLine({center.x - d, center.y - d}, {center.x + d, center.y + d}, xc, 1.f);
            dl->AddLine({center.x + d, center.y - d}, {center.x - d, center.y + d}, xc, 1.f);
        }
    }

    void DrawRingCenter(ImDrawList* dl, ImVec2 c, float r) {
        const auto& st = Style();
        dl->AddCircleFilled(c, r, st.ringCenterFill, 16);
        dl->AddCircle(c, r, st.ringCenterBorder, 16, 1.f);
    }

    void DrawModifierWidget(ImDrawList* dl, ImVec2 c, bool modHeld) {
        const auto& st = StyleConfig::Get();

        std::uint8_t alpha = 0;
        switch (st.modifierWidgetVisibility) {
            case ModifierWidgetVisibility::Never:
                alpha = 0;
                break;
            case ModifierWidgetVisibility::HideOnPress:
                alpha = modHeld ? 0 : 255;
                break;
            case ModifierWidgetVisibility::Always:
            default:
                alpha = 255;
                break;
        }
        if (alpha == 0) return;

        const auto& icon = ResolveModifierIcon();
        if (!icon.valid()) return;

        const float r = st.modifierWidgetRadius;
        const float iconSz = r * 2.f;
        const ImVec2 pos = {c.x + st.modifierWidgetOffsetX, c.y + st.modifierWidgetOffsetY};
        dl->AddImage(reinterpret_cast<ImTextureID>(icon.texture), {pos.x - iconSz * 0.5f, pos.y - iconSz * 0.5f},
                     {pos.x + iconSz * 0.5f, pos.y + iconSz * 0.5f}, {0.f, 0.f}, {1.f, 1.f},
                     IM_COL32(255, 255, 255, alpha));
    }

    void DrawSlotHotkeyIcons(ImDrawList* dl, ImVec2 center, float slotR, int slotIndex) {
        const auto& cfg = IntegratedMagic::GetMagicConfig();
        const auto& st = StyleConfig::Get();
        const auto iconType = st.buttonIconType;

        constexpr float kIconSize = 28.f;
        constexpr float kSpacing = 2.f;
        constexpr float kMarginY = 4.f;

        struct KeyEntry {
            bool isGamepad;
            int code;
        };
        KeyEntry keys[3]{};
        int keyCount = 0;

        const auto& ic = cfg.slotInput[static_cast<std::size_t>(slotIndex)];
        if (iconType == ButtonIconType::Keyboard) {
            int codes[3] = {ic.KeyboardScanCode1.load(std::memory_order_relaxed),
                            ic.KeyboardScanCode2.load(std::memory_order_relaxed),
                            ic.KeyboardScanCode3.load(std::memory_order_relaxed)};
            for (int c : codes)
                if (c >= 0 && keyCount < 3) keys[keyCount++] = {false, c};
        } else {
            int codes[3] = {ic.GamepadButton1.load(std::memory_order_relaxed),
                            ic.GamepadButton2.load(std::memory_order_relaxed),
                            ic.GamepadButton3.load(std::memory_order_relaxed)};
            for (int c : codes)
                if (c >= 0 && keyCount < 3) keys[keyCount++] = {true, c};
        }
        if (keyCount == 0) return;

        const TextureManager::Image* imgs[3]{};
        int validCount = 0;
        for (int k = 0; k < keyCount; ++k) {
            const auto& img = keys[k].isGamepad ? TextureManager::GetGamepadButtonIcon(keys[k].code, iconType)
                                                : TextureManager::GetKeyboardIcon(keys[k].code);
            if (img.valid()) imgs[validCount++] = &img;
        }
        if (validCount == 0) return;

        const float totalW = validCount * kIconSize + (validCount - 1) * kSpacing;
        const float startX = center.x - totalW * 0.5f;
        const float startY = center.y - slotR - kMarginY - kIconSize;

        for (int k = 0; k < validCount; ++k) {
            const float x = startX + k * (kIconSize + kSpacing);
            dl->AddImage(reinterpret_cast<ImTextureID>(imgs[k]->texture), {x, startY},
                         {x + kIconSize, startY + kIconSize}, {0.f, 0.f}, {1.f, 1.f}, IM_COL32(255, 255, 255, 255));
        }
    }

    void DrawSlotButtonLabel(ImDrawList* dl, ImVec2 center, float slotR, int slotIndex, ImVec2 hudOrigin, float alpha) {
        if (alpha <= 0.f) return;

        const auto& cfg = IntegratedMagic::GetMagicConfig();
        const auto& st = StyleConfig::Get();
        const auto iconType = st.buttonIconType;
        const int modPos =
            (iconType == ButtonIconType::Keyboard) ? cfg.modifierKeyboardPosition : cfg.modifierGamepadPosition;
        const bool suppressMod = (st.modifierWidgetVisibility != ModifierWidgetVisibility::Never) && (modPos > 0);

        struct KeyEntry {
            bool isGamepad;
            int code;
        };
        KeyEntry keys[3]{};
        int keyCount = 0;

        const auto& ic = cfg.slotInput[static_cast<std::size_t>(slotIndex)];
        if (iconType == ButtonIconType::Keyboard) {
            int codes[3] = {ic.KeyboardScanCode1.load(std::memory_order_relaxed),
                            ic.KeyboardScanCode2.load(std::memory_order_relaxed),
                            ic.KeyboardScanCode3.load(std::memory_order_relaxed)};
            for (int k = 0; k < 3; ++k) {
                if (codes[k] < 0) continue;
                if (suppressMod && (k + 1) == modPos) continue;
                if (keyCount < 3) keys[keyCount++] = {false, codes[k]};
            }
        } else {
            int codes[3] = {ic.GamepadButton1.load(std::memory_order_relaxed),
                            ic.GamepadButton2.load(std::memory_order_relaxed),
                            ic.GamepadButton3.load(std::memory_order_relaxed)};
            for (int k = 0; k < 3; ++k) {
                if (codes[k] < 0) continue;
                if (suppressMod && (k + 1) == modPos) continue;
                if (keyCount < 3) keys[keyCount++] = {true, codes[k]};
            }
        }
        if (keyCount == 0) return;

        const TextureManager::Image* imgs[3]{};
        int validCount = 0;
        for (int k = 0; k < keyCount; ++k) {
            const auto& img = keys[k].isGamepad ? TextureManager::GetGamepadButtonIcon(keys[k].code, iconType)
                                                : TextureManager::GetKeyboardIcon(keys[k].code);
            if (img.valid()) imgs[validCount++] = &img;
        }
        if (validCount == 0) return;

        const float iconSize = st.buttonLabelIconSize;
        const float spacing = st.buttonLabelIconSpacing;
        const float margin = st.buttonLabelMargin;
        const float totalW = validCount * iconSize + (validCount - 1) * spacing;

        float startX = 0.f, startY = 0.f;
        switch (st.buttonLabelCorner) {
            case ButtonLabelCorner::Top:
                startX = center.x - totalW * 0.5f;
                startY = center.y - slotR - margin - iconSize;
                break;
            case ButtonLabelCorner::Bottom:
                startX = center.x - totalW * 0.5f;
                startY = center.y + slotR + margin;
                break;
            case ButtonLabelCorner::Left:
                startX = center.x - slotR - margin - totalW;
                startY = center.y - iconSize * 0.5f;
                break;
            case ButtonLabelCorner::Right:
                startX = center.x + slotR + margin;
                startY = center.y - iconSize * 0.5f;
                break;
            case ButtonLabelCorner::TowardCenter: {
                const float dx = hudOrigin.x - center.x, dy = hudOrigin.y - center.y;
                if (const float len = std::sqrt(dx * dx + dy * dy); len > 0.5f) {
                    const float ax = center.x + (dx / len) * (slotR + margin + iconSize * 0.5f);
                    const float ay = center.y + (dy / len) * (slotR + margin + iconSize * 0.5f);
                    startX = ax - totalW * 0.5f;
                    startY = ay - iconSize * 0.5f;
                } else {
                    startX = center.x - totalW * 0.5f;
                    startY = center.y - slotR - margin - iconSize;
                }
                break;
            }
            case ButtonLabelCorner::AwayFromCenter: {
                const float dx = center.x - hudOrigin.x, dy = center.y - hudOrigin.y;
                if (const float len = std::sqrt(dx * dx + dy * dy); len > 0.5f) {
                    const float ax = center.x + (dx / len) * (slotR + margin + iconSize * 0.5f);
                    const float ay = center.y + (dy / len) * (slotR + margin + iconSize * 0.5f);
                    startX = ax - totalW * 0.5f;
                    startY = ay - iconSize * 0.5f;
                } else {
                    startX = center.x - totalW * 0.5f;
                    startY = center.y + slotR + margin;
                }
                break;
            }
        }
        startX += st.buttonLabelOffsetX;
        startY += st.buttonLabelOffsetY;

        const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.f));
        for (int k = 0; k < validCount; ++k) {
            const float x = startX + k * (iconSize + spacing);
            dl->AddImage(reinterpret_cast<ImTextureID>(imgs[k]->texture), {x, startY},
                         {x + iconSize, startY + iconSize}, {0.f, 0.f}, {1.f, 1.f}, tint);
        }
    }

    void DrawSmallHUD(const ImGuiIO& io) {
        const auto& st = Style();
        const int n = static_cast<int>(Slots::GetSlotCount());
        const int activeSlot = MagicState::Get().ActiveSlot();
        const bool modHeld = !MagicState::Get().IsActive() && Input::IsModifierHeld();

        SlotAnimator::Update(n, activeSlot, modHeld, st.hudLayout, st.gridColumns);

        static float s_labelAlpha[SlotLayout::kMaxSlots]{};
        {
            using clock = std::chrono::steady_clock;
            static clock::time_point s_last = clock::now();
            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - s_last).count();
            s_last = now;
            if (dt < 0.f || dt > 0.25f) dt = 0.f;

            const bool slotActive = MagicState::Get().IsActive();
            const float fadeSpeed = st.buttonLabelFadeTime > 0.f ? 1.f / st.buttonLabelFadeTime : 9999.f;

            for (int i = 0; i < n; ++i) {
                float target = 0.f;
                switch (st.buttonLabelVisibility) {
                    case ButtonLabelVisibility::Never:
                        target = 0.f;
                        break;
                    case ButtonLabelVisibility::Always:
                        target = 1.f;
                        break;
                    case ButtonLabelVisibility::OnModifier:
                        target = (modHeld && !slotActive) ? 1.f : 0.f;
                        break;
                }
                s_labelAlpha[i] += (target - s_labelAlpha[i]) * std::min(dt * fadeSpeed, 1.f);
                s_labelAlpha[i] = std::clamp(s_labelAlpha[i], 0.f, 1.f);
            }
            for (int i = n; i < SlotLayout::kMaxSlots; ++i) s_labelAlpha[i] = 0.f;
        }

        float maxScale = SlotAnimator::MaxPossibleScale();
        for (int i = 0; i < n; ++i) maxScale = std::max(maxScale, SlotAnimator::GetScale(i));

        const LayoutVec2 animHalf = [&] {
            LayoutVec2 h = SlotLayout::BoundingHalf(st.hudLayout, n, st.slotRadius * maxScale, st.ringRadius,
                                                    st.slotSpacing, st.gridColumns);
            float extraY = kGlowPad;
            if (st.showSpellNamesInHud) {
                const bool iconsVisible = st.buttonLabelVisibility == ButtonLabelVisibility::Always ||
                                          (st.buttonLabelVisibility == ButtonLabelVisibility::OnModifier && modHeld);
                const float iconReserve = iconsVisible ? (st.buttonLabelIconSize + st.buttonLabelMargin) : 0.f;
                const float textReserve = ImGui::GetTextLineHeight() * 3.f + 4.f + iconReserve;
                extraY += textReserve;
            }
            return LayoutVec2{h.x + kGlowPad, h.y + extraY};
        }();

        const ImVec2 hudOrigin = ComputeHudCenter(io, {animHalf.x, animHalf.y});

        LayoutVec2 relPos[SlotLayout::kMaxSlots]{};
        SlotLayout::Compute(st.hudLayout, n, st.slotRadius, st.ringRadius, st.slotSpacing, st.gridColumns, relPos);

        auto ScaledCenter = [&](int idx) -> ImVec2 {
            const float scale = SlotAnimator::GetScale(idx);
            const float rx = relPos[idx].x, ry = relPos[idx].y;
            const float len = std::sqrt(rx * rx + ry * ry);
            const float push = (scale - 1.f) * st.slotRadius;
            if (len > 0.5f) return {hudOrigin.x + rx + (rx / len) * push, hudOrigin.y + ry + (ry / len) * push};
            return {hudOrigin.x + rx, hudOrigin.y + ry};
        };

        ImGui::SetNextWindowPos({hudOrigin.x - animHalf.x, hudOrigin.y - animHalf.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({animHalf.x * 2.f, animHalf.y * 2.f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.f);
        ImGui::Begin(kHudWindowID, nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoInputs);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (SlotLayout::HasCenter(st.hudLayout)) DrawRingCenter(dl, hudOrigin);

        auto DrawSlot = [&](int i, bool active) {
            const ImVec2 center = ScaledCenter(i);
            const float slotR = st.slotRadius * SlotAnimator::GetScale(i);
            const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
            const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
            const auto shID = Slots::GetSlotShout(i);
            auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
            auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
            const bool is2H = !shID && !rID && lSp && SpellClassify::IsTwoHandedSpell(lSp);
            DrawSlotVisual(dl, center, slotR, active, is2H ? nullptr : rSp, is2H ? nullptr : lSp, is2H ? lID : shID);

            if (st.showSpellNamesInHud && !MagicState::Get().IsActive()) {
                const bool iconsVisible = st.buttonLabelVisibility == ButtonLabelVisibility::Always ||
                                          (st.buttonLabelVisibility == ButtonLabelVisibility::OnModifier && modHeld);
                const float iconReserve = iconsVisible ? (st.buttonLabelIconSize + st.buttonLabelMargin) : 0.f;
                const float slotTop = center.y - slotR - iconReserve;

                if (shID || is2H) {
                    const RE::FormID dispID = shID ? shID : lID;
                    auto* f = RE::TESForm::LookupByID(dispID);
                    const char* name = f ? f->GetName() : "";
                    DrawWrappedLabelAbove(name, center.x - slotR, slotR * 2.f, slotTop, 4.f, true);
                } else if (rSp || lSp) {
                    const bool sameSpell = rSp && lSp && (rSp->GetFormID() == lSp->GetFormID());
                    const bool onlyOne = (rSp != nullptr) != (lSp != nullptr);

                    if (sameSpell || onlyOne) {
                        const RE::SpellItem* sp = rSp ? rSp : lSp;
                        DrawWrappedLabelAbove(sp->GetName(), center.x - slotR, slotR * 2.f, slotTop, 4.f, true);
                    } else {
                        constexpr float kPipeGap = 6.f;
                        const float pipeW = ImGui::CalcTextSize("|").x;
                        const float halfPipe = pipeW * 0.5f;

                        DrawWrappedLabelAbove(lSp->GetName(), center.x - slotR, slotR - halfPipe - kPipeGap, slotTop);

                        const float pipeH = ImGui::GetTextLineHeight();
                        ImGui::SetCursorScreenPos({center.x - halfPipe, slotTop - 4.f - pipeH});

                        DrawWrappedLabelAbove(rSp->GetName(), center.x + halfPipe + kPipeGap,
                                              slotR - halfPipe - kPipeGap, slotTop);
                    }
                }
            }
        };

        for (int i = 0; i < n; ++i)
            if (i != activeSlot) DrawSlot(i, false);
        if (activeSlot >= 0 && activeSlot < n) DrawSlot(activeSlot, true);

        for (int i = 0; i < n; ++i)
            if (i != activeSlot)
                DrawSlotButtonLabel(dl, ScaledCenter(i), st.slotRadius * SlotAnimator::GetScale(i), i, hudOrigin,
                                    s_labelAlpha[i]);
        if (activeSlot >= 0 && activeSlot < n)
            DrawSlotButtonLabel(dl, ScaledCenter(activeSlot), st.slotRadius * SlotAnimator::GetScale(activeSlot),
                                activeSlot, hudOrigin, s_labelAlpha[activeSlot]);

        DrawModifierWidget(dl, hudOrigin, Input::IsModifierHeld() || MagicState::Get().IsActive());

        ImGui::End();
    }
}