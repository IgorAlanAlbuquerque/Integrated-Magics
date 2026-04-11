#include "SlotDrawer.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <numbers>

#include "Application/HudController.h"
#include "Config/ConfigAdapter.h"
#include "Config/StyleConfig.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "State/SlotCostUtil.h"
#include "State/SpellClassify.h"
#include "State/State.h"
#include "UI/HudState.h"
#include "UI/HudTextUtil.h"
#include "UI/PolyFill.h"
#include "UI/SlotAnimator.h"
#include "UI/SlotLayout.h"
#include "UI/TextureManager.h"
#include "imgui_internal.h"

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
            const auto& bindings = IntegratedMagic::Config::MagicConfigAdapter::Get();
            const auto& st = StyleConfig::Get();

            if (st.buttonIconType == ButtonIconType::Keyboard) {
                const int kbPos = bindings.ModifierKbPosition();
                if (kbPos <= 0) return kEmpty;
                const auto binding = bindings.GetSlotBinding(0);
                const int sc = kbPos == 1 ? binding.kb[0] : kbPos == 2 ? binding.kb[1] : binding.kb[2];
                if (sc < 0) return kEmpty;
                return TextureManager::GetKeyboardIcon(sc);
            } else {
                const int gpPos = bindings.ModifierGpPosition();
                if (gpPos <= 0) return kEmpty;
                const auto binding = bindings.GetSlotBinding(0);
                const int idx = gpPos == 1 ? binding.gp[0] : gpPos == 2 ? binding.gp[1] : binding.gp[2];
                if (idx < 0) return kEmpty;
                return TextureManager::GetGamepadButtonIcon(idx, st.buttonIconType);
            }
        }
    }

    inline ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
        const auto l8 = [](ImU32 ca, ImU32 cb, float tt) {
            return ca + static_cast<int>((static_cast<int>(cb) - static_cast<int>(ca)) * tt);
        };
        return IM_COL32(l8(a & 0xFF, b & 0xFF, t), l8((a >> 8) & 0xFF, (b >> 8) & 0xFF, t),
                        l8((a >> 16) & 0xFF, (b >> 16) & 0xFF, t), l8((a >> 24) & 0xFF, (b >> 24) & 0xFF, t));
    }

    void FillSlotShapeGradient(ImDrawList* dl, ImVec2 center, float r) {
        const auto& st = Style();
        const ImU32 gStart = st.slotGradientStart;
        const ImU32 gEnd = st.slotGradientEnd;

        const auto& shape = st.slotShape;
        std::vector<ImVec2> pts;
        if (shape.vertices.size() >= 3) {
            pts.reserve(shape.vertices.size());
            for (const auto& v : shape.vertices) pts.push_back({center.x + v.x * r, center.y + v.y * r});
        } else {
            constexpr int kSeg = 48;
            pts.reserve(kSeg);
            for (int i = 0; i < kSeg; ++i) {
                const float a = 2.f * kPI * static_cast<float>(i) / kSeg;
                pts.push_back({center.x + std::cos(a) * r, center.y + std::sin(a) * r});
            }
        }
        const auto n = static_cast<int>(pts.size());
        if (n < 3) return;

        ImU32 centerCol;
        std::vector<ImU32> edgeCol(n);

        if (st.slotGradientType == GradientType::Radial) {
            const ImVec2 gc = {center.x, center.y - st.slotGradientRadialOffset * r};
            centerCol = gStart;
            for (int i = 0; i < n; ++i) {
                const float dx = pts[i].x - gc.x;
                const float dy = pts[i].y - gc.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float t = std::clamp(dist / r, 0.f, 1.f);
                edgeCol[i] = LerpColor(gStart, gEnd, t);
            }

            const ImVec2 uv = dl->_Data->TexUvWhitePixel;
            dl->PrimReserve(n * 3, n * 3);
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                dl->PrimVtx(gc, uv, centerCol);
                dl->PrimVtx(pts[i], uv, edgeCol[i]);
                dl->PrimVtx(pts[j], uv, edgeCol[j]);
            }
        } else {
            const float rad = st.slotGradientAngle * kPI / 180.f;
            const float ax = std::cos(rad);
            const float ay = std::sin(rad);
            float minP = 0.f;
            float maxP = 0.f;
            std::vector<float> proj(n);
            for (int i = 0; i < n; ++i) {
                proj[i] = (pts[i].x - center.x) * ax + (pts[i].y - center.y) * ay;
                minP = std::min(minP, proj[i]);
                maxP = std::max(maxP, proj[i]);
            }
            const float range = maxP - minP;
            const float centerProj = 0.f;
            const float ct = (range > 0.f) ? (centerProj - minP) / range : 0.5f;
            centerCol = LerpColor(gStart, gEnd, std::clamp(ct, 0.f, 1.f));
            for (int i = 0; i < n; ++i) {
                const float t = (range > 0.f) ? (proj[i] - minP) / range : 0.5f;
                edgeCol[i] = LerpColor(gStart, gEnd, std::clamp(t, 0.f, 1.f));
            }
            const ImVec2 uv = dl->_Data->TexUvWhitePixel;
            dl->PrimReserve(n * 3, n * 3);
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                dl->PrimVtx(center, uv, centerCol);
                dl->PrimVtx(pts[i], uv, edgeCol[i]);
                dl->PrimVtx(pts[j], uv, edgeCol[j]);
            }
        }
    }

    ImU32 ComputeIconTint() {
        const auto& st = Style();
        float r = 1.f;
        float g = 1.f;
        float b = 1.f;
        const float alpha = st.iconAlpha / 255.f;

        if ((st.iconTintColor >> 24) > 0) {
            const float ta = ((st.iconTintColor >> 24) & 0xFF) / 255.f * std::clamp(st.iconTintStrength, 0.f, 1.f);
            const float tr = (st.iconTintColor & 0xFF) / 255.f;
            const float tg = ((st.iconTintColor >> 8) & 0xFF) / 255.f;
            const float tb = ((st.iconTintColor >> 16) & 0xFF) / 255.f;
            r = 1.f - ta + tr * ta;
            g = 1.f - ta + tg * ta;
            b = 1.f - ta + tb * ta;
        }

        if (st.iconSaturation < 255) {
            const float sat = st.iconSaturation / 255.f;
            const float gray = r * 0.299f + g * 0.587f + b * 0.114f;
            r = std::lerp(gray, r, sat);
            g = std::lerp(gray, g, sat);
            b = std::lerp(gray, b, sat);
        }

        const float bri = st.iconBrightness / 128.f;
        r = std::clamp(r * bri, 0.f, 1.f);
        g = std::clamp(g * bri, 0.f, 1.f);
        b = std::clamp(b * bri, 0.f, 1.f);

        return IM_COL32(static_cast<int>(r * 255.f), static_cast<int>(g * 255.f), static_cast<int>(b * 255.f),
                        static_cast<int>(alpha * 255.f));
    }

    void PathSlotShape(ImDrawList* dl, ImVec2 center, float r) {
        const auto& st = Style();
        const auto& shape = st.slotShape;
        if (shape.useCustomShape && shape.vertices.size() >= 3) {
            for (const auto& v : shape.vertices) dl->PathLineTo({center.x + v.x * r, center.y + v.y * r});
        } else {
            switch (st.slotCornerStyle) {
                using enum IntegratedMagic::CornerStyle;
                case Square:
                    dl->PathRect({center.x - r, center.y - r}, {center.x + r, center.y + r}, 0.f);
                    break;
                case Notched:
                case Chamfered: {
                    const float c = std::clamp(st.slotCornerSize, 0.f, r * 0.8f);
                    dl->PathLineTo({center.x - r + c, center.y - r});
                    dl->PathLineTo({center.x + r - c, center.y - r});
                    dl->PathLineTo({center.x + r, center.y - r + c});
                    dl->PathLineTo({center.x + r, center.y + r - c});
                    dl->PathLineTo({center.x + r - c, center.y + r});
                    dl->PathLineTo({center.x - r + c, center.y + r});
                    dl->PathLineTo({center.x - r, center.y + r - c});
                    dl->PathLineTo({center.x - r, center.y - r + c});
                    break;
                }
                case Round:
                default:
                    dl->PathArcTo(center, r, 0.f, 2.f * kPI, 48);
                    break;
            }
        }
    }

    void FillSlotShape(ImDrawList* dl, ImVec2 center, float r, ImU32 col) {
        const auto& st = Style();

        if (st.slotGradientType != GradientType::None) {
            FillSlotShapeGradient(dl, center, r);
            return;
        }
        const auto& shape = st.slotShape;
        if (shape.useCustomShape && shape.vertices.size() >= 3) {
            if (PolyFill::IsConvex(shape.vertices)) {
                for (const auto& v : shape.vertices) dl->PathLineTo({center.x + v.x * r, center.y + v.y * r});
                dl->PathFillConvex(col);
            } else {
                for (const auto& t : PolyFill::Triangulate(shape.vertices, center.x, center.y, r))
                    dl->AddTriangleFilled({t.ax, t.ay}, {t.bx, t.by}, {t.cx, t.cy}, col);
            }
        } else {
            PathSlotShape(dl, center, r);
            dl->PathFillConvex(col);
        }
    }

    void StrokeSlotShape(ImDrawList* dl, ImVec2 center, float r, ImU32 col, float thickness) {
        PathSlotShape(dl, center, r);
        dl->PathStroke(col, ImDrawFlags_Closed, thickness);
    }

    void DrawGlowShape(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol) {
        const auto& st = Style();
        const ImU32 base = glowCol & 0x00FFFFFFu;
        const auto baseA = static_cast<int>(((glowCol >> 24) & 0xFF) * std::clamp(st.glowIntensity, 0.f, 2.f));
        if (baseA == 0) return;

        const int n = std::clamp(static_cast<int>(st.glowLayers), 1, 5);
        const float step = std::max(0.5f, st.glowRadius);
        const bool custom = st.slotShape.useCustomShape && st.slotShape.vertices.size() >= 3;

        if (st.glowStyle == GlowStyle::Fill || st.glowStyle == GlowStyle::Both) {
            const ImU32 fillCol = base | (static_cast<ImU32>(baseA / 4) << 24);
            const float fillR = r + step * static_cast<float>(n);
            if (custom) {
                for (const auto& t : PolyFill::Triangulate(st.slotShape.vertices, c.x, c.y, fillR))
                    dl->AddTriangleFilled({t.ax, t.ay}, {t.bx, t.by}, {t.cx, t.cy}, fillCol);
            } else {
                PathSlotShape(dl, c, fillR);
                dl->PathFillConvex(fillCol);
            }
        }

        if (st.glowStyle == GlowStyle::Ring || st.glowStyle == GlowStyle::Both) {
            for (int i = n; i >= 1; --i) {
                const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
                const float layerR = r + static_cast<float>(i) * step;
                if (custom)
                    StrokeSlotShape(dl, c, layerR, layer, 1.2f);
                else
                    dl->AddCircle(c, layerR, layer, 48, 1.2f);
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
                     {0.f, 0.f}, {1.f, 1.f}, ComputeIconTint());
    }

    void DrawCrackOverlay(ImDrawList* dl, ImVec2 center, float r) {
        struct CrackPoint {
            float x, y;
        };
        struct Crack {
            CrackPoint pts[4];
            int count;
        };

        static constexpr Crack kCracks[] = {
            {{{-0.10f, -0.20f}, {-0.28f, -0.42f}, {-0.40f, -0.55f}, {-0.55f, -0.70f}}, 4},
            {{{-0.10f, -0.20f}, {0.12f, -0.38f}, {0.30f, -0.52f}, {0.45f, -0.65f}}, 4},
            {{{-0.10f, -0.20f}, {-0.30f, 0.00f}, {-0.45f, 0.15f}, {-0.60f, 0.25f}}, 4},
            {{{-0.10f, -0.20f}, {0.15f, 0.08f}, {0.35f, 0.25f}, {0.50f, 0.40f}}, 4},
            {{{-0.10f, -0.20f}, {-0.05f, 0.15f}, {-0.20f, 0.45f}, {-0.15f, 0.75f}}, 4},
            {{{-0.40f, -0.55f}, {-0.50f, -0.72f}, {-0.30f, -0.88f}}, 3},
            {{{0.30f, -0.52f}, {0.55f, -0.58f}, {0.70f, -0.40f}}, 3},
        };

        const ImU32 col = IM_COL32(255, 255, 255, 90);
        const ImU32 colFaint = IM_COL32(255, 255, 255, 40);

        for (int i = 0; i < static_cast<int>(std::size(kCracks)); ++i) {
            const auto& crack = kCracks[i];
            ImVec2 pts[4];
            for (int k = 0; k < crack.count; ++k)
                pts[k] = {center.x + crack.pts[k].x * r, center.y + crack.pts[k].y * r};
            dl->AddPolyline(pts, crack.count, i < 5 ? col : colFaint, 0, 1.f);
        }
    }

    void DrawSlotVisual(ImDrawList* dl, ImVec2 center, float r, bool isActive, RE::SpellItem const* rSpell,
                        RE::SpellItem const* lSpell, RE::FormID shoutFormID, bool forceOffset, bool canCast) {
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
                             {center.x + half, center.y + half}, {0.f, 0.f}, {1.f, 1.f}, ComputeIconTint());
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
            const double pulse = 0.65 + 0.35 * std::sin(t * static_cast<double>(st.pulseSpeed));
            const ImU32 baseRGB = st.slotRingActive & 0x00FFFFFFu;
            const auto pulseAlpha = static_cast<int>(st.slotRingActiveAlpha * pulse);
            const ImU32 ring = baseRGB | (static_cast<ImU32>(pulseAlpha) << 24);

            StrokeSlotShape(dl, center, r, ring, st.slotRingWidthActive);

            const ImU32 halo = baseRGB | (static_cast<ImU32>(static_cast<int>(pulseAlpha * 0.28f)) << 24);
            StrokeSlotShape(dl, center, r + st.slotRingWidthActive + 0.5f, halo, 1.0f);
        } else {
            StrokeSlotShape(dl, center, r, st.slotRingInactive, st.slotRingWidth);
        }

        if ((st.slotOuterRingColor >> 24) > 0) {
            const float activeW = isActive ? st.slotRingWidthActive : st.slotRingWidth;
            const float outerR = r + activeW + 1.5f;
            StrokeSlotShape(dl, center, outerR, st.slotOuterRingColor, st.slotOuterRingWidth);
        }

        if (!rSpell && !lSpell && !shoutFormID) {
            const float d = r * 0.32f;
            const ImU32 xc = st.emptySlotColor;
            dl->AddLine({center.x - d, center.y - d}, {center.x + d, center.y + d}, xc, 1.f);
            dl->AddLine({center.x + d, center.y - d}, {center.x - d, center.y + d}, xc, 1.f);
        }

        if (!canCast) DrawCrackOverlay(dl, center, r);
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
        const auto& bindings = IntegratedMagic::Config::MagicConfigAdapter::Get();
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

        const auto binding = bindings.GetSlotBinding(slotIndex);
        if (iconType == ButtonIconType::Keyboard) {
            for (int c : binding.kb)
                if (c >= 0 && keyCount < 3) keys[keyCount++] = {false, c};
        } else {
            for (int c : binding.gp)
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

        const auto& bindings = IntegratedMagic::Config::MagicConfigAdapter::Get();
        const auto& st = StyleConfig::Get();
        const auto iconType = st.buttonIconType;
        const int modPos =
            (iconType == ButtonIconType::Keyboard) ? bindings.ModifierKbPosition() : bindings.ModifierGpPosition();
        const bool suppressMod = (st.modifierWidgetVisibility != ModifierWidgetVisibility::Never) && (modPos > 0);

        struct KeyEntry {
            bool isGamepad;
            int code;
        };
        KeyEntry keys[3]{};
        int keyCount = 0;

        const auto binding = bindings.GetSlotBinding(slotIndex);
        if (iconType == ButtonIconType::Keyboard) {
            for (int k = 0; k < 3; ++k) {
                if (binding.kb[k] < 0) continue;
                if (suppressMod && (k + 1) == modPos) continue;
                if (keyCount < 3) keys[keyCount++] = {false, binding.kb[k]};
            }
        } else {
            for (int k = 0; k < 3; ++k) {
                if (binding.gp[k] < 0) continue;
                if (suppressMod && (k + 1) == modPos) continue;
                if (keyCount < 3) keys[keyCount++] = {true, binding.gp[k]};
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

        float startX = 0.f;
        float startY = 0.f;
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
                const float dx = hudOrigin.x - center.x;
                const float dy = hudOrigin.y - center.y;
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
                const float dx = center.x - hudOrigin.x;
                const float dy = center.y - hudOrigin.y;
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
        const auto n = static_cast<int>(Slots::GetSlotCount());
        const int activeSlot = MagicState::Get().ActiveSlot();
        const bool modHeld = !MagicState::Get().IsActive() && Application::HudController::Get().IsModifierHeld();

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

        struct SlotManaAnim {
            bool wasCastable = true;
            float pulseT = -1.f;
        };
        static SlotManaAnim s_manaAnim[SlotLayout::kMaxSlots]{};
        {
            using clock = std::chrono::steady_clock;
            static clock::time_point s_manaLast = clock::now();
            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - s_manaLast).count();
            s_manaLast = now;
            if (dt < 0.f || dt > 0.25f) dt = 0.f;

            constexpr float kPulseDuration = 0.30f;

            for (int i = 0; i < n; ++i) {
                const auto afford = ComputeSlotAffordability(i);
                const bool castable = !afford.hasSpells || afford.canCast;
                auto& anim = s_manaAnim[i];

                if (castable && !anim.wasCastable && anim.pulseT < 0.f) anim.pulseT = 0.f;

                anim.wasCastable = castable;

                if (anim.pulseT >= 0.f) {
                    anim.pulseT += dt / kPulseDuration;
                    if (anim.pulseT >= 1.f) anim.pulseT = -1.f;
                }
            }
            for (int i = n; i < SlotLayout::kMaxSlots; ++i) s_manaAnim[i] = {};
        }

        auto GetManaPulseScale = [](int i) -> float {
            const float t = s_manaAnim[i].pulseT;
            if (t < 0.f) return 1.f;
            return 1.f + 0.28f * std::sin(t * kPI);
        };

        float maxScale = SlotAnimator::MaxPossibleScale();
        for (int i = 0; i < n; ++i) maxScale = std::max(maxScale, SlotAnimator::GetScale(i));

        const float scalePad = (SlotAnimator::MaxPossibleScale() - 1.f) * st.slotRadius + kGlowPad;
        const LayoutVec2 baseHalf =
            SlotLayout::BoundingHalf(st.hudLayout, n, st.slotRadius, st.ringRadius, st.slotSpacing, st.gridColumns);

        float textPadTop = 0.f, textPadBottom = 0.f, textPadLeft = 0.f, textPadRight = 0.f;
        if (st.showSpellNamesInHud) {
            const float textReserve = ImGui::GetTextLineHeight() * 2.f + 8.f + st.spellNamePadding;
            switch (st.spellNamePosition) {
                case ButtonLabelCorner::Top:
                    textPadTop = textReserve;
                    break;
                case ButtonLabelCorner::Bottom:
                    textPadBottom = textReserve;
                    break;
                case ButtonLabelCorner::Left:
                    textPadLeft = textReserve;
                    break;
                case ButtonLabelCorner::Right:
                    textPadRight = textReserve;
                    break;
                default:
                    textPadTop = textPadBottom = textPadLeft = textPadRight = textReserve;
                    break;
            }
        }

        const LayoutVec2 stableHalf = {baseHalf.x + scalePad + std::max(textPadLeft, textPadRight),
                                       baseHalf.y + scalePad + std::max(textPadTop, textPadBottom)};

        ImGuiIO fakeIo = io;
        fakeIo.DisplaySize = IntegratedMagic::HUD::GetDisplaySize();
        const ImVec2 hudOrigin = ComputeHudCenter(fakeIo, {stableHalf.x, stableHalf.y});

        LayoutVec2 relPos[SlotLayout::kMaxSlots]{};
        SlotLayout::Compute(st.hudLayout, n, st.slotRadius, st.ringRadius, st.slotSpacing, st.gridColumns, relPos);

        auto ScaledCenter = [&](int idx) -> ImVec2 {
            const float scale = SlotAnimator::GetScale(idx);
            const float rx = relPos[idx].x;
            const float ry = relPos[idx].y;
            const float len = std::sqrt(rx * rx + ry * ry);
            if (len <= 0.5f) return {hudOrigin.x + rx, hudOrigin.y + ry};
            const float scaledLen = len + (scale - 1.f) * st.slotRadius;
            return {hudOrigin.x + (rx / len) * scaledLen, hudOrigin.y + (ry / len) * scaledLen};
        };

        const float winOffsetX = (textPadRight - textPadLeft) * 0.5f;
        const float winOffsetY = (textPadBottom - textPadTop) * 0.5f;

        ImGui::SetNextWindowPos({hudOrigin.x - stableHalf.x + winOffsetX, hudOrigin.y - stableHalf.y + winOffsetY},
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize({stableHalf.x * 2.f, stableHalf.y * 2.f}, ImGuiCond_Always);
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
            const float slotR = st.slotRadius * SlotAnimator::GetScale(i) * GetManaPulseScale(i);
            const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
            const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
            const auto shID = Slots::GetSlotShout(i);
            auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
            auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
            const bool is2H = !shID && !rID && lSp && SpellClassify::IsTwoHandedSpell(lSp);

            const bool canCast = s_manaAnim[i].wasCastable;

            DrawSlotVisual(dl, center, slotR, active, is2H ? nullptr : rSp, is2H ? nullptr : lSp, is2H ? lID : shID,
                           false, canCast);

            if (st.showSpellNamesInHud && !MagicState::Get().IsActive()) {
                const ImVec2 toCenter = [&]() -> ImVec2 {
                    const float dx = hudOrigin.x - center.x;
                    const float dy = hudOrigin.y - center.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    return len > 0.5f ? ImVec2{dx / len, dy / len} : ImVec2{0.f, -1.f};
                }();

                auto drawLabel = [&](const char* name) {
                    DrawSpellLabel(name, center, slotR, toCenter, st.spellNamePosition, st.spellNamePadding);
                };

                if (shID || is2H) {
                    const RE::FormID dispID = shID ? shID : lID;
                    auto const* f = RE::TESForm::LookupByID(dispID);
                    drawLabel(f ? f->GetName() : "");
                } else if (rSp || lSp) {
                    const bool same = rSp && lSp && (rSp->GetFormID() == lSp->GetFormID());
                    const bool onlyOne = (rSp != nullptr) != (lSp != nullptr);
                    if (same || onlyOne) {
                        drawLabel((rSp ? rSp : lSp)->GetName());
                    } else {
                        std::string combined = std::string(lSp->GetName()) + " | " + rSp->GetName();
                        drawLabel(combined.c_str());
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

        DrawModifierWidget(dl, hudOrigin,
                           Application::HudController::Get().IsModifierHeld() || MagicState::Get().IsActive());

        ImGui::End();
    }
}