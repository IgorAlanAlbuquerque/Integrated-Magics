#include "PopupDrawer.h"

#include <imgui.h>

#include <cmath>
#include <numbers>
#include <string>

#include "Config/Config.h"
#include "Config/Slots.h"
#include "HudState.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "SlotDrawer.h"
#include "State/Assign.h"
#include "State/SpellClassify.h"
#include "State/State.h"
#include "Strings.h"
#include "UI/HoveredForm.h"
#include "UI/PolyFill.h"
#include "UI/SlotLayout.h"
#include "UI/StyleConfig.h"
#include "UI/TextureManager.h"

namespace IntegratedMagic::HUD::PopupDrawer {

    namespace {
        constexpr float kPI = std::numbers::pi_v<float>;
        constexpr float kGlowPad = 16.f;
        constexpr const char* kPopupID = "##IMAGIC_DETAIL";

        inline const StyleConfig& Style() { return StyleConfig::Get(); }

        inline int ModeToIndex(ActivationMode m) {
            switch (m) {
                case ActivationMode::Hold:
                    return 0;
                case ActivationMode::Press:
                    return 1;
                default:
                    return 2;
            }
        }
        inline ActivationMode IndexToMode(int i) {
            switch (i) {
                case 0:
                    return ActivationMode::Hold;
                case 1:
                    return ActivationMode::Press;
                default:
                    return ActivationMode::Automatic;
            }
        }

        inline bool ManualClick(bool clicked, ImVec2 pos, ImVec2 size) {
            if (!clicked) return false;
            return g_mousePos.x >= pos.x && g_mousePos.x < pos.x + size.x && g_mousePos.y >= pos.y &&
                   g_mousePos.y < pos.y + size.y;
        }
        inline bool ManualHover(ImVec2 pos, ImVec2 size) {
            return g_mousePos.x >= pos.x && g_mousePos.x < pos.x + size.x && g_mousePos.y >= pos.y &&
                   g_mousePos.y < pos.y + size.y;
        }
        void MouseTooltip(const char* text) {
            ImGui::SetNextWindowPos({g_mousePos.x + 16.f, g_mousePos.y + 8.f}, ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(text);
            ImGui::EndTooltip();
        }

        void FillSector(ImDrawList* dl, ImVec2 c, float r, float a0, float a1, ImU32 col, int segs = 24) {
            dl->PathLineTo(c);
            dl->PathArcTo(c, r, a0, a1, segs);
            dl->PathLineTo(c);
            dl->PathFillConvex(col);
        }

        void FillSlotShapeHighlight(ImDrawList* dl, ImVec2 center, float r, ImU32 col) {
            const auto& shape = StyleConfig::Get().slotShape;
            if (shape.vertices.size() >= 3) {
                for (const auto& t : PolyFill::Triangulate(shape.vertices, center.x, center.y, r))
                    dl->AddTriangleFilled({t.ax, t.ay}, {t.bx, t.by}, {t.cx, t.cy}, col);
            } else {
                dl->AddCircleFilled(center, r, col, 48);
            }
        }

        void FillSlotHalfHighlight(ImDrawList* dl, ImVec2 center, float r, bool rightHalf, ImU32 col) {
            const auto& shape = StyleConfig::Get().slotShape;
            if (shape.vertices.size() >= 3) {
                const float sign = rightHalf ? 1.f : -1.f;
                std::vector<SlotShapeVertex> clipped;
                const auto& verts = shape.vertices;
                const int n = static_cast<int>(verts.size());
                for (int i = 0; i < n; ++i) {
                    const SlotShapeVertex& a = verts[i];
                    const SlotShapeVertex& b = verts[(i + 1) % n];
                    const float da = a.x * sign;
                    const float db = b.x * sign;
                    if (da >= 0.f) clipped.push_back(a);
                    if ((da >= 0.f) != (db >= 0.f)) {
                        const float t = da / (da - db);
                        clipped.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
                    }
                }

                if (clipped.size() >= 3) {
                    for (const auto& t : PolyFill::Triangulate(clipped, center.x, center.y, r))
                        dl->AddTriangleFilled({t.ax, t.ay}, {t.bx, t.by}, {t.cx, t.cy}, col);
                }
            } else {
                if (rightHalf)
                    FillSector(dl, center, r, -kPI * 0.5f, kPI * 0.5f, col);
                else
                    FillSector(dl, center, r, kPI * 0.5f, kPI * 1.5f, col);
            }
        }

        float DrawActionBadge(ImDrawList* dl, ImVec2 origin, float iconSize, const TextureManager::Image& icon,
                              const char* fallbackLabel, const char* actionLabel, ImU32 bgColor) {
            constexpr float kGap = 6.f;

            if (icon.valid()) {
                const ImVec2 ip0 = origin;
                const ImVec2 ip1 = {ip0.x + iconSize, ip0.y + iconSize};
                dl->AddImage(reinterpret_cast<ImTextureID>(icon.texture), ip0, ip1, {0.f, 0.f}, {1.f, 1.f},
                             IM_COL32(255, 255, 255, 255));
                const ImVec2 ts = ImGui::CalcTextSize(actionLabel);
                ImGui::SetCursorScreenPos({ip1.x + kGap, ip0.y + (iconSize - ts.y) * 0.5f});
                ImGui::TextDisabled("%s", actionLabel);
                return iconSize + kGap + ts.x + kGap * 2.f;
            } else {
                constexpr float kPad = 4.f;
                const float chipW = iconSize + kPad * 2.f;
                const float chipH = iconSize + kPad * 2.f;
                const ImVec2 p0 = origin;
                const ImVec2 p1 = {p0.x + chipW, p0.y + chipH};
                dl->AddRectFilled(p0, p1, bgColor, 4.f, 0);
                dl->AddRect(p0, p1, IM_COL32(200, 200, 200, 80), 4.f, 0, 1.f);
                const ImVec2 lts = ImGui::CalcTextSize(fallbackLabel);
                ImGui::SetCursorScreenPos({p0.x + (chipW - lts.x) * 0.5f, p0.y + (chipH - lts.y) * 0.5f});
                ImGui::TextUnformatted(fallbackLabel);
                const ImVec2 ts = ImGui::CalcTextSize(actionLabel);
                ImGui::SetCursorScreenPos({p1.x + kGap, p0.y + (chipH - ts.y) * 0.5f});
                ImGui::TextDisabled("%s", actionLabel);
                return chipW + kGap + ts.x + kGap * 2.f;
            }
        }
    }

    void DrawOverlayAndCursor(ImVec2 displaySize, ImVec2 cursorPos) {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled({0.f, 0.f}, {displaySize.x, displaySize.y}, IM_COL32(0, 0, 0, Style().overlayAlpha), 0.f, 0);

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 pts[3] = {{cursorPos.x, cursorPos.y},
                               {cursorPos.x + 10.f, cursorPos.y + 4.f},
                               {cursorPos.x + 4.f, cursorPos.y + 10.f}};
        fg->AddTriangleFilled(pts[0], pts[1], pts[2], IM_COL32(255, 255, 255, 230));
        fg->AddTriangle(pts[0], pts[1], pts[2], IM_COL32(0, 0, 0, 200), 1.2f);
    }

    void DrawPopupActionHints(ImDrawList* dl, ImVec2 popupPos, ImVec2 popupSize, bool isShoutOrPower, bool hoverRight) {
        const auto& st = StyleConfig::Get();
        const auto iconType = st.buttonIconType;
        constexpr float kIconSize = 32.f;
        constexpr float kBarPad = 8.f;
        constexpr float kSpacing = 16.f;

        const TextureManager::Image* assignIcon = nullptr;
        const TextureManager::Image* clearIcon = nullptr;
        const char* assignFallback = "LMB";
        const char* clearFallback = "RMB";
        static const TextureManager::Image kEmpty{};

        if (iconType == ButtonIconType::Xbox) {
            assignIcon = &TextureManager::GetGamepadButtonIcon(12, ButtonIconType::Xbox);
            clearIcon = &TextureManager::GetGamepadButtonIcon(11, ButtonIconType::Xbox);
            assignFallback = "X";
            clearFallback = "B";
        } else if (iconType == ButtonIconType::PlayStation) {
            assignIcon = &TextureManager::GetGamepadButtonIcon(12, ButtonIconType::PlayStation);
            clearIcon = &TextureManager::GetGamepadButtonIcon(11, ButtonIconType::PlayStation);
            assignFallback = "\xE2\x96\xA1";
            clearFallback = "\xE2\x97\x8B";
        } else {
            assignIcon = &TextureManager::GetKeyboardIcon(kMouseLeftIndex);
            clearIcon = &TextureManager::GetKeyboardIcon(kMouseRightIndex);
        }

        const std::string assignText = isShoutOrPower
                                           ? Strings::Get("Popup_Hint_Assign", "Assign")
                                           : (hoverRight ? Strings::Get("Popup_Hint_AssignRight", "Assign Right")
                                                         : Strings::Get("Popup_Hint_AssignLeft", "Assign Left"));
        const std::string clearText = Strings::Get("Popup_Hint_Clear", "Clear");

        const ImVec2 assignTS = ImGui::CalcTextSize(assignText.c_str());
        const ImVec2 clearTS = ImGui::CalcTextSize(clearText.c_str());
        const float totalW = kIconSize + 6.f + assignTS.x + 12.f + kSpacing + kIconSize + 6.f + clearTS.x + 12.f;
        const ImVec2 barOrigin = {popupPos.x + (popupSize.x - totalW) * 0.5f,
                                  popupPos.y + popupSize.y - kIconSize - kBarPad};

        float x = barOrigin.x;
        x += DrawActionBadge(dl, {x, barOrigin.y}, kIconSize, assignIcon ? *assignIcon : kEmpty, assignFallback,
                             assignText.c_str(), IM_COL32(40, 100, 200, 160));
        x += kSpacing;
        DrawActionBadge(dl, {x, barOrigin.y}, kIconSize, clearIcon ? *clearIcon : kEmpty, clearFallback,
                        clearText.c_str(), IM_COL32(160, 40, 40, 160));
    }

    void DrawSpellModeWidget(ImDrawList* dl, bool clicked, ImVec2 origin, float availW, std::uint32_t formID,
                             const char*) {
        if (!formID) return;
        auto* form = RE::TESForm::LookupByID(formID);
        auto s = SpellSettingsDB::Get().GetOrCreate(formID, form);

        static const char* kLabels[] = {"H", "P", "A"};
        const std::string kTipHold = Strings::Get("Mode_Hold", "Hold");
        const std::string kTipPress = Strings::Get("Mode_Press", "Press");
        const std::string kTipAuto = Strings::Get("Mode_Auto", "Auto");
        const char* kTips[] = {kTipHold.c_str(), kTipPress.c_str(), kTipAuto.c_str()};

        constexpr int kNumModes = 3;
        const float btnR = 8.f;
        const float stepX = availW / kNumModes;

        for (int m = 0; m < kNumModes; ++m) {
            const ImVec2 bc = {origin.x + stepX * m + stepX * 0.5f, origin.y + btnR};
            const bool cur = (ModeToIndex(s.mode) == m);
            const bool hov = ManualHover({bc.x - btnR, bc.y - btnR}, {btnR * 2.f, btnR * 2.f});

            dl->AddCircleFilled(bc, btnR,
                                cur   ? IM_COL32(120, 90, 20, 220)
                                : hov ? IM_COL32(60, 60, 60, 180)
                                      : IM_COL32(30, 30, 30, 160),
                                16);
            dl->AddCircle(bc, btnR, cur ? IM_COL32(220, 170, 50, 220) : IM_COL32(90, 90, 90, 160), 16, 1.2f);

            ImGui::SetCursorScreenPos({bc.x - 4.f, bc.y - 7.f});
            cur ? ImGui::TextUnformatted(kLabels[m]) : ImGui::TextDisabled("%s", kLabels[m]);

            if (hov) MouseTooltip(kTips[m]);
            if (ManualClick(clicked, {bc.x - btnR, bc.y - btnR}, {btnR * 2.f, btnR * 2.f})) {
                s.mode = IndexToMode(m);
                SpellSettingsDB::Get().Set(formID, s);
            }
        }

        if (s.mode == ActivationMode::Hold || s.mode == ActivationMode::Press) {
            const ImVec2 cc = {origin.x + availW * 0.5f, origin.y + btnR * 2.f + 6.f + btnR};
            const bool hov = ManualHover({cc.x - btnR, cc.y - btnR}, {btnR * 2.f, btnR * 2.f});

            dl->AddCircleFilled(cc, btnR, hov ? IM_COL32(50, 50, 50, 180) : IM_COL32(25, 25, 25, 160), 16);
            dl->AddCircle(cc, btnR, IM_COL32(90, 90, 90, 160), 16, 1.2f);

            if (s.autoAttack) {
                const float d = btnR * 0.55f;
                dl->AddLine({cc.x - d, cc.y}, {cc.x - d * 0.2f, cc.y + d * 0.8f}, IM_COL32(80, 200, 80, 255), 2.f);
                dl->AddLine({cc.x - d * 0.2f, cc.y + d * 0.8f}, {cc.x + d, cc.y - d * 0.6f}, IM_COL32(80, 200, 80, 255),
                            2.f);
            } else {
                const float d = btnR * 0.45f;
                dl->AddLine({cc.x - d, cc.y - d}, {cc.x + d, cc.y + d}, IM_COL32(160, 80, 80, 200), 1.5f);
                dl->AddLine({cc.x + d, cc.y - d}, {cc.x - d, cc.y + d}, IM_COL32(160, 80, 80, 200), 1.5f);
            }

            ImGui::SetCursorScreenPos({cc.x + btnR + 2.f, cc.y - 7.f});
            ImGui::TextDisabled("AA");

            if (ManualClick(clicked, {cc.x - btnR, cc.y - btnR}, {btnR * 2.f, btnR * 2.f})) {
                s.autoAttack = !s.autoAttack;
                SpellSettingsDB::Get().Set(formID, s);
            }
        }
    }

    void DrawDetailPopup() {
        const ImGuiIO& io = ImGui::GetIO();

        if (g_popupJustOpened.exchange(false)) g_mousePos = {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};

        const bool clicked = g_mouseClicked.exchange(false, std::memory_order_relaxed);
        const bool rightClicked = g_mouseRightClicked.exchange(false, std::memory_order_relaxed);

        bool hintsVisible = false;
        bool hintsShout = false;
        bool hintsHoverRight = false;

        const int n = static_cast<int>(Slots::GetSlotCount());
        const auto& st = Style();
        const float dynPopupR = [&] {
            const float minR =
                n > 1 ? (st.popupSlotRadius + st.popupSlotGap * 0.5f) / std::sin(kPI / static_cast<float>(n))
                      : st.popupRingRadius;
            return std::max(st.popupRingRadius, minR);
        }();

        const LayoutVec2 bh =
            SlotLayout::BoundingHalf(st.popupLayout, n, st.popupSlotRadius, dynPopupR, st.popupSlotGap, st.gridColumns);
        const float popupHalfX = bh.x + kGlowPad + st.modeWidgetW + 12.f;
        const float popupHalfY = bh.y + kGlowPad + st.modeWidgetW + 12.f;
        const ImVec2 popupSize = {popupHalfX * 2.f, popupHalfY * 2.f + 48.f};
        const ImVec2 popupPos = {io.DisplaySize.x * 0.5f - popupSize.x * 0.5f,
                                 io.DisplaySize.y * 0.5f - popupSize.y * 0.5f};
        const ImVec2 popupEnd = {popupPos.x + popupSize.x, popupPos.y + popupSize.y};
        const ImVec2 ringCenter = {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};

        LayoutVec2 relPos[SlotLayout::kMaxSlots]{};
        SlotLayout::Compute(st.popupLayout, n, st.popupSlotRadius, dynPopupR, st.popupSlotGap, st.gridColumns, relPos);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always, {0.f, 0.f});
        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.12f);

        if (ImGui::Begin(kPopupID, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoDecoration)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int activeSlot = MagicState::Get().ActiveSlot();

            const auto hovType = HoveredForm::GetHoveredMagicType();
            const bool hovIsShoutOrPow =
                hovType == HoveredForm::MagicType::Shout || hovType == HoveredForm::MagicType::Power;
            const bool hovIsTwoHanded = hovType == HoveredForm::MagicType::TwoHandedSpell;
            const bool hovIsRightOnly = hovType == HoveredForm::MagicType::RightOnlySpell;
            const bool hovIsLeftOnly = hovType == HoveredForm::MagicType::LeftOnlySpell;
            const bool hovIsFullSlot = hovIsShoutOrPow || hovIsTwoHanded;

            for (int i = 0; i < n; ++i) {
                const ImVec2 center = {ringCenter.x + relPos[i].x, ringCenter.y + relPos[i].y};
                const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
                const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
                const auto shoutID = Slots::GetSlotShout(i);
                auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;

                const bool slotIs2H = !shoutID && !rID && lSp && SpellClassify::IsTwoHandedSpell(lSp);
                const RE::FormID dispShoutID = shoutID ? shoutID : (slotIs2H ? lID : 0);

                SlotDrawer::DrawSlotVisual(dl, center, st.popupSlotRadius, activeSlot == i, slotIs2H ? nullptr : rSp,
                                           slotIs2H ? nullptr : lSp, dispShoutID);
                SlotDrawer::DrawSlotHotkeyIcons(dl, center, st.popupSlotRadius, i);

                if (shoutID || slotIs2H) {
                    auto* f = RE::TESForm::LookupByID(dispShoutID);
                    const char* name = f ? f->GetName() : "???";
                    const char* prefix = shoutID ? Strings::Get("Popup_ShoutPrefix", "[S]").c_str() : "[2H]";
                    ImGui::SetCursorScreenPos({center.x - st.popupSlotRadius, center.y - st.popupSlotRadius - 16.f});
                    ImGui::TextDisabled("%s %s", prefix, name);
                } else if (rSp || lSp) {
                    const std::string label =
                        std::string(lSp ? lSp->GetName() : "---") + " | " + (rSp ? rSp->GetName() : "---");
                    ImGui::SetCursorScreenPos({center.x - st.popupSlotRadius, center.y - st.popupSlotRadius - 16.f});
                    ImGui::TextDisabled("%s", label.c_str());
                }

                {
                    const float dx = g_mousePos.x - center.x;
                    const float dy = g_mousePos.y - center.y;
                    if ((dx * dx + dy * dy) < (st.popupSlotRadius * st.popupSlotRadius)) {
                        if (hovIsFullSlot) {
                            FillSlotShapeHighlight(dl, center, st.popupSlotRadius - 1.f, IM_COL32(255, 200, 80, 40));
                            if (clicked) {
                                hovIsTwoHanded ? MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Left)
                                               : MagicAssign::TryAssignHoveredShoutToSlot(i);
                            }
                            if (rightClicked) {
                                shoutID ? MagicAssign::TryClearSlotShout(i)
                                        : (MagicAssign::TryClearSlotHand(i, Slots::Hand::Right),
                                           MagicAssign::TryClearSlotHand(i, Slots::Hand::Left));
                            }
                            hintsVisible = true;
                            hintsShout = true;
                            hintsHoverRight = false;
                        } else {
                            const bool hoverRight = hovIsRightOnly ? true : hovIsLeftOnly ? false : (dx >= 0.f);
                            const ImU32 hlCol = IM_COL32(255, 200, 80, 40);
                            hoverRight ? FillSlotHalfHighlight(dl, center, st.popupSlotRadius - 1.f, true, hlCol)
                                       : FillSlotHalfHighlight(dl, center, st.popupSlotRadius - 1.f, false, hlCol);
                            if (clicked) {
                                hoverRight ? MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Right)
                                           : MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Left);
                            }
                            if (rightClicked) {
                                if (shoutID)
                                    MagicAssign::TryClearSlotShout(i);
                                else if (slotIs2H) {
                                    MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                    MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                } else
                                    hoverRight ? MagicAssign::TryClearSlotHand(i, Slots::Hand::Right)
                                               : MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                            }
                            hintsVisible = true;
                            hintsShout = false;
                            hintsHoverRight = hoverRight;
                        }
                    }
                }

                {
                    const float wy = center.y + st.popupSlotRadius + 4.f;
                    if (shoutID)
                        DrawSpellModeWidget(dl, clicked, {center.x - st.modeWidgetW * 0.5f, wy}, st.modeWidgetW,
                                            shoutID, std::to_string(i).append("S").c_str());
                    else {
                        if (rID)
                            DrawSpellModeWidget(dl, clicked, {center.x + 2.f, wy}, st.modeWidgetW, rID,
                                                std::to_string(i).append("R").c_str());
                        if (lID)
                            DrawSpellModeWidget(dl, clicked, {center.x - st.modeWidgetW - 2.f, wy}, st.modeWidgetW, lID,
                                                std::to_string(i).append("L").c_str());
                    }
                }
            }

            if (hintsVisible) DrawPopupActionHints(dl, popupPos, popupSize, hintsShout, hintsHoverRight);

            const bool mouseOutside = clicked && (g_mousePos.x < popupPos.x || g_mousePos.x > popupEnd.x ||
                                                  g_mousePos.y < popupPos.y || g_mousePos.y > popupEnd.y);

            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || mouseOutside) g_popupOpen.store(false);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        DrawOverlayAndCursor({io.DisplaySize.x, io.DisplaySize.y}, g_mousePos);
    }
}