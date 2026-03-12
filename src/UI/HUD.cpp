#include "HUD.h"

#include <atomic>
#include <cmath>
#include <numbers>

#include "Config/Slots.h"
#include "Input/Assign.h"
#include "Input/Input.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "RE/B/BSFixedString.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"
#include "SKSEMenuFramework.h"
#include "State/State.h"
#include "UI/SlotAnimator.h"
#include "UI/StyleConfig.h"
#include "UI/TextureManager.h"

namespace ImGui = ImGuiMCP;
namespace DL = ImGuiMCP::ImDrawListManager;

namespace IntegratedMagic::HUD {
    namespace {
        using namespace ImGuiMCP;

        constexpr float kPI = std::numbers::pi_v<float>;
        constexpr float kGlowPad = 16.f;
        constexpr const char* kDetailPopupID = "##IMAGIC_DETAIL";
        constexpr const char* kHudWindowID = "##IMAGIC_HUD";

        inline const StyleConfig& Style() { return StyleConfig::Get(); }

        static SKSEMenuFramework::Model::WindowInterface* g_popupWindow = nullptr;

        static ImVec2 g_mousePos = {0.f, 0.f};
        static std::atomic_bool g_mouseClicked{false};
        static std::atomic_bool g_mouseRightClicked{false};
        static std::atomic_bool g_popupJustOpened{false};
        static std::atomic_bool g_hudVisible{true};

        bool IsHardBlocked() {
            if (!RE::PlayerCharacter::GetSingleton()) return true;
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString mainMenu{"Main Menu"};
            static const RE::BSFixedString loadingMenu{"Loading Menu"};
            static const RE::BSFixedString faderMenu{"Fader Menu"};
            return ui->IsMenuOpen(mainMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(faderMenu);
        }

        bool IsSoftBlocked() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            static const RE::BSFixedString tweenMenu{"TweenMenu"};
            static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
            static const RE::BSFixedString statsMenu{"StatsMenu"};
            static const RE::BSFixedString mapMenu{"MapMenu"};
            static const RE::BSFixedString journalMenu{"Journal Menu"};
            static const RE::BSFixedString containerMenu{"ContainerMenu"};
            static const RE::BSFixedString barterMenu{"BarterMenu"};
            static const RE::BSFixedString craftingMenu{"Crafting Menu"};
            static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
            static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
            static const RE::BSFixedString dialogueMenu{"Dialogue Menu"};
            static const RE::BSFixedString console{"Console"};
            static const RE::BSFixedString mcm{"Mod Configuration Menu"};
            static const RE::BSFixedString bestiary{"BestiaryMenu"};
            static const RE::BSFixedString ostim{"OstimSceneMenu"};
            static const RE::BSFixedString dialogueTopicMenu{"Dialogue Topic Menu"};
            return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(statsMenu) || ui->IsMenuOpen(mapMenu) ||
                   ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) ||
                   ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(lockpickingMenu) || ui->IsMenuOpen(sleepWaitMenu) ||
                   ui->IsMenuOpen(dialogueMenu) || ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) ||
                   ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(tweenMenu) || ui->IsMenuOpen(bestiary) ||
                   ui->IsMenuOpen(ostim) || ui->IsMenuOpen(dialogueTopicMenu);
        }

        bool IsInMagicMenu() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return false;
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            return ui->IsMenuOpen(magicMenu);
        }

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

        void FillSector(ImDrawList* dl, ImVec2 c, float r, float a0, float a1, ImU32 col, int segs = 24) {
            DL::PathLineTo(dl, c);
            DL::PathArcTo(dl, c, r, a0, a1, segs);
            DL::PathLineTo(dl, c);
            DL::PathFillConvex(dl, col);
        }

        void DrawGlow(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol) {
            const ImU32 base = glowCol & 0x00FFFFFFu;
            const auto baseA = static_cast<int>((glowCol >> 24) & 0xFF);
            for (int i = 5; i >= 1; --i) {
                const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
                DL::AddCircle(dl, c, r + static_cast<float>(i) * 2.5f, layer, 48, 1.2f);
            }
        }

        void DrawSpellIcon(ImDrawList* dl, const RE::SpellItem* spell, float cx, float cy, float iconSize) {
            const auto& img = TextureManager::GetSpellIcon(spell);
            if (!img.valid()) return;
            const float half = iconSize * 0.5f;
            const ImVec2 p0{cx - half, cy - half};
            const ImVec2 p1{cx + half, cy + half};
            DL::AddImage(dl, reinterpret_cast<ImTextureID>(img.texture), p0, p1, {0.f, 0.f}, {1.f, 1.f},
                         IM_COL32(255, 255, 255, Style().iconAlpha));
        }

        void DrawSlotVisual(ImDrawList* dl, ImVec2 center, float r, bool isActive, RE::SpellItem const* rSpell,
                            RE::SpellItem const* lSpell, RE::FormID shoutFormID = 0) {
            const auto rPal = SpellPalette(rSpell);
            const auto lPal = SpellPalette(lSpell);

            if (isActive) {
                DrawGlow(dl, center, r, rPal.glow);
                DrawGlow(dl, center, r, lPal.glow);
            }

            const auto& st = Style();
            DL::AddCircleFilled(dl, center, r, isActive ? st.slotBgActive : st.slotBgInactive, 48);

            const float iconSize = r * st.iconSizeFactor;

            if (shoutFormID) {
                const auto& img = TextureManager::GetIconForForm(shoutFormID);
                if (img.valid()) {
                    const float half = iconSize * 0.6f;
                    const ImVec2 p0{center.x - half, center.y - half};
                    const ImVec2 p1{center.x + half, center.y + half};
                    DL::AddImage(dl, reinterpret_cast<ImTextureID>(img.texture), p0, p1, {0.f, 0.f}, {1.f, 1.f},
                                 IM_COL32(255, 255, 255, st.iconAlpha));
                }
            } else {
                const float iconOffset = r * st.iconOffsetFactor;
                if (rSpell) DrawSpellIcon(dl, rSpell, center.x + iconOffset, center.y, iconSize);
                if (lSpell) DrawSpellIcon(dl, lSpell, center.x - iconOffset, center.y, iconSize);
            }

            if (isActive) {
                const double t = ImGui::GetTime();
                const double pulse = 0.65 + 0.35 * std::sin(t * 4.5);
                const ImU32 ring =
                    IM_COL32(255, static_cast<int>(210 * pulse), static_cast<int>(50 * pulse), st.slotRingActiveAlpha);
                DL::AddCircle(dl, center, r, ring, 48, 2.5f);
                DL::AddCircle(dl, center, r + 3.f, (ring & 0x00FFFFFFu) | (70u << 24), 48, 1.0f);
            } else {
                DL::AddCircle(dl, center, r, st.slotRingInactive, 48, 1.2f);
            }

            if (!rSpell && !lSpell && !shoutFormID) {
                const float d = r * 0.32f;
                const ImU32 xc = st.emptySlotColor;
                DL::AddLine(dl, {center.x - d, center.y - d}, {center.x + d, center.y + d}, xc, 1.f);
                DL::AddLine(dl, {center.x + d, center.y - d}, {center.x - d, center.y + d}, xc, 1.f);
            }
        }

        inline float DynamicRingRadius(int n, float slotR, float baseR, float gap = 8.f) {
            if (n <= 1) return baseR;
            const float minR = (slotR + gap * 0.5f) / std::sin(kPI / static_cast<float>(n));
            return std::max(baseR, minR);
        }

        ImVec2 SlotCenter(ImVec2 ringCenter, float ringRadius, int i, int n) {
            const float angle = kPI + (2.f * kPI / static_cast<float>(n)) * static_cast<float>(i);
            return {ringCenter.x + ringRadius * std::cos(angle), ringCenter.y + ringRadius * std::sin(angle)};
        }

        void DrawRingCenter(ImDrawList* dl, ImVec2 c, float r = 4.f) {
            const auto& st = Style();
            DL::AddCircleFilled(dl, c, r, st.ringCenterFill, 16);
            DL::AddCircle(dl, c, r, st.ringCenterBorder, 16, 1.f);
        }

        void DrawOverlayAndCursor(ImVec2 displaySize, ImVec2 cursorPos) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            DL::AddRectFilled(bg, {0.f, 0.f}, {displaySize.x, displaySize.y}, IM_COL32(0, 0, 0, Style().overlayAlpha),
                              0.f, 0);

            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const ImVec2 pts[3] = {{cursorPos.x, cursorPos.y},
                                   {cursorPos.x + 10.f, cursorPos.y + 4.f},
                                   {cursorPos.x + 4.f, cursorPos.y + 10.f}};
            DL::AddTriangleFilled(fg, pts[0], pts[1], pts[2], IM_COL32(255, 255, 255, 230));
            DL::AddTriangle(fg, pts[0], pts[1], pts[2], IM_COL32(0, 0, 0, 200), 1.2f);
        }

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

        void DrawSpellModeWidget(ImDrawList* dl, bool clicked, ImVec2 origin, float availW, std::uint32_t formID,
                                 const char*) {
            if (!formID) return;
            auto s = SpellSettingsDB::Get().GetOrCreate(formID);

            static const char* kLabels[] = {"H", "P", "A"};
            static const char* kTips[] = {"Hold", "Press", "Auto"};
            constexpr int kNumModes = 3;
            const float btnR = 8.f;
            const float stepX = availW / kNumModes;

            for (int m = 0; m < kNumModes; ++m) {
                const ImVec2 bc = {origin.x + stepX * m + stepX * 0.5f, origin.y + btnR};
                const bool cur = (ModeToIndex(s.mode) == m);
                const bool hov = ManualHover({bc.x - btnR, bc.y - btnR}, {btnR * 2.f, btnR * 2.f});

                DL::AddCircleFilled(dl, bc, btnR,
                                    cur   ? IM_COL32(120, 90, 20, 220)
                                    : hov ? IM_COL32(60, 60, 60, 180)
                                          : IM_COL32(30, 30, 30, 160),
                                    16);
                DL::AddCircle(dl, bc, btnR, cur ? IM_COL32(220, 170, 50, 220) : IM_COL32(90, 90, 90, 160), 16, 1.2f);

                ImGui::SetCursorScreenPos({bc.x - 4.f, bc.y - 7.f});
                if (cur)
                    ImGui::TextUnformatted(kLabels[m]);
                else
                    ImGui::TextDisabled("%s", kLabels[m]);

                if (hov) {
                    ImGui::SetCursorScreenPos({bc.x + btnR + 2.f, bc.y - 7.f});
                    ImGui::TextDisabled("%s", kTips[m]);
                }

                if (ManualClick(clicked, {bc.x - btnR, bc.y - btnR}, {btnR * 2.f, btnR * 2.f})) {
                    s.mode = IndexToMode(m);
                    SpellSettingsDB::Get().Set(formID, s);
                }
            }

            if (s.mode == ActivationMode::Hold) {
                const ImVec2 cc = {origin.x + availW * 0.5f, origin.y + btnR * 2.f + 6.f + btnR};
                const bool hov = ManualHover({cc.x - btnR, cc.y - btnR}, {btnR * 2.f, btnR * 2.f});

                DL::AddCircleFilled(dl, cc, btnR, hov ? IM_COL32(50, 50, 50, 180) : IM_COL32(25, 25, 25, 160), 16);
                DL::AddCircle(dl, cc, btnR, IM_COL32(90, 90, 90, 160), 16, 1.2f);

                if (s.autoAttack) {
                    const float d = btnR * 0.55f;
                    DL::AddLine(dl, {cc.x - d, cc.y}, {cc.x - d * 0.2f, cc.y + d * 0.8f}, IM_COL32(80, 200, 80, 255),
                                2.f);
                    DL::AddLine(dl, {cc.x - d * 0.2f, cc.y + d * 0.8f}, {cc.x + d, cc.y - d * 0.6f},
                                IM_COL32(80, 200, 80, 255), 2.f);
                } else {
                    const float d = btnR * 0.45f;
                    DL::AddLine(dl, {cc.x - d, cc.y - d}, {cc.x + d, cc.y + d}, IM_COL32(160, 80, 80, 200), 1.5f);
                    DL::AddLine(dl, {cc.x + d, cc.y - d}, {cc.x - d, cc.y + d}, IM_COL32(160, 80, 80, 200), 1.5f);
                }

                ImGui::SetCursorScreenPos({cc.x + btnR + 2.f, cc.y - 7.f});
                ImGui::TextDisabled("AA");

                if (ManualClick(clicked, {cc.x - btnR, cc.y - btnR}, {btnR * 2.f, btnR * 2.f})) {
                    s.autoAttack = !s.autoAttack;
                    SpellSettingsDB::Get().Set(formID, s);
                }
            }
        }

        // Returns the center point of the HUD ring based on anchor + offset config.
        // 'half' is the half-size of the bounding box (ringRadius + maxSlotR + glowPad).
        ImVec2 ComputeHudCenter(const ImGuiIO* io, float half) {
            const float W  = io->DisplaySize.x;
            const float H  = io->DisplaySize.y;
            const auto& st = Style();
            const float ox = st.hudOffsetX;
            const float oy = st.hudOffsetY;

            // Margin keeps the HUD fully inside the screen edge.
            const float m = half + 4.f;

            switch (st.hudAnchor) {
                using enum HudAnchor;
                case TopLeft:      return {m + ox,           m + oy};
                case TopCenter:    return {W * 0.5f + ox,    m + oy};
                case TopRight:     return {W - m + ox,       m + oy};
                case MiddleLeft:   return {m + ox,           H * 0.5f + oy};
                case Center:       return {W * 0.5f + ox,    H * 0.5f + oy};
                case MiddleRight:  return {W - m + ox,       H * 0.5f + oy};
                case BottomLeft:   return {m + ox,           H - m + oy};
                case BottomCenter: return {W * 0.5f + ox,    H - m + oy};
                case BottomRight:
                default:           return {W - m + ox,       H - m + oy};
            }
        }

        void DrawSmallHUD(ImGuiIO const* io) {
            const auto& st = Style();
            const int n          = static_cast<int>(Slots::GetSlotCount());
            const int activeSlot = MagicState::Get().ActiveSlot();

            SlotAnimator::Update(n, activeSlot, /*modifierHeld=*/false);

            // Ring center is anchored to a FIXED half (base slotRadius only).
            // This prevents the center from drifting as slots animate.
            const float fixedHalf   = st.ringRadius + st.slotRadius + kGlowPad;
            const ImVec2 ringCenter = ComputeHudCenter(io, fixedHalf);

            // Window is sized to the ANIMATED max so expanded slots are never clipped.
            float maxScale = SlotAnimator::MaxPossibleScale();
            for (int i = 0; i < n; ++i) maxScale = std::max(maxScale, SlotAnimator::GetScale(i));
            const float animHalf = st.ringRadius + st.slotRadius * maxScale + kGlowPad;

            ImGui::SetNextWindowPos({ringCenter.x - animHalf, ringCenter.y - animHalf}, ImGuiCond_Always, {0.f, 0.f});
            ImGui::SetNextWindowSize({animHalf * 2.f, animHalf * 2.f}, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.f);

            ImGui::Begin(kHudWindowID, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoInputs);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            DrawRingCenter(dl, ringCenter);

            const float dynR = DynamicRingRadius(n, st.slotRadius, st.ringRadius);

            // Two-pass draw: inactive slots first, active slot last (painter's algorithm).
            // This ensures the active slot always renders on top of its neighbors.
            for (int i = 0; i < n; ++i) {
                if (i == activeSlot) continue;
                const ImVec2 center  = SlotCenter(ringCenter, dynR, i, n);
                const float  slotR   = st.slotRadius * SlotAnimator::GetScale(i);
                const auto rID       = Slots::GetSlotSpell(i, Slots::Hand::Right);
                const auto lID       = Slots::GetSlotSpell(i, Slots::Hand::Left);
                const auto shoutID   = Slots::GetSlotShout(i);
                auto const* rSp      = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                auto const* lSp      = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
                DrawSlotVisual(dl, center, slotR, false, rSp, lSp, shoutID);
            }

            if (activeSlot >= 0 && activeSlot < n) {
                const ImVec2 center  = SlotCenter(ringCenter, dynR, activeSlot, n);
                const float  slotR   = st.slotRadius * SlotAnimator::GetScale(activeSlot);
                const auto rID       = Slots::GetSlotSpell(activeSlot, Slots::Hand::Right);
                const auto lID       = Slots::GetSlotSpell(activeSlot, Slots::Hand::Left);
                const auto shoutID   = Slots::GetSlotShout(activeSlot);
                auto const* rSp      = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                auto const* lSp      = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
                DrawSlotVisual(dl, center, slotR, true, rSp, lSp, shoutID);
            }

            ImGui::End();
        }

        void DrawDetailPopup() {
            ImGuiIO const* io = ImGui::GetIO();

            if (g_popupJustOpened.exchange(false)) {
                g_mousePos = {io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f};
            }

            const bool clicked = g_mouseClicked.exchange(false, std::memory_order_relaxed);
            const bool rightClicked = g_mouseRightClicked.exchange(false, std::memory_order_relaxed);

            const int n = static_cast<int>(Slots::GetSlotCount());
            const auto& st = Style();
            const float dynPopupR = DynamicRingRadius(n, st.popupSlotRadius, st.popupRingRadius);
            const float popupHalf = dynPopupR + st.popupSlotRadius + kGlowPad + st.modeWidgetW + 12.f;
            const ImVec2 popupSize = {popupHalf * 2.f, popupHalf * 2.f + 48.f};
            const ImVec2 popupPos = {io->DisplaySize.x * 0.5f - popupSize.x * 0.5f,
                                     io->DisplaySize.y * 0.5f - popupSize.y * 0.5f};
            const ImVec2 popupEnd = {popupPos.x + popupSize.x, popupPos.y + popupSize.y};
            const ImVec2 ringCenter = {io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f};

            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});

            ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always, {0.f, 0.f});
            ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.12f);

            if (ImGui::Begin(kDetailPopupID, nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDecoration)) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const int activeSlot = MagicState::Get().ActiveSlot();

                DrawRingCenter(dl, ringCenter, 6.f);

                const auto hovType = MagicAssign::GetHoveredMagicType();
                const bool hovIsShoutOrPower =
                    hovType == MagicAssign::HoveredMagicType::Shout || hovType == MagicAssign::HoveredMagicType::Power;

                for (int i = 0; i < n; ++i) {
                    const ImVec2 center = SlotCenter(ringCenter, dynPopupR, i, n);
                    const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
                    const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
                    const auto shoutID = Slots::GetSlotShout(i);
                    auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                    auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;

                    DrawSlotVisual(dl, center, st.popupSlotRadius, activeSlot == i, rSp, lSp, shoutID);

                    {
                        const std::string slotLabel = "Slot " + std::to_string(i + 1);
                        ImVec2 textSize{};
                        ImGui::CalcTextSize(&textSize, slotLabel.c_str(), nullptr, false, -1.0f);
                        ImGui::SetCursorScreenPos(
                            {center.x - textSize.x * 0.5f, center.y - st.popupSlotRadius - textSize.y - 4.f});
                        ImGui::TextDisabled("%s", slotLabel.c_str());
                    }

                    {
                        if (shoutID) {
                            auto* shoutForm = RE::TESForm::LookupByID(shoutID);
                            const char* shoutName = shoutForm ? shoutForm->GetName() : "???";
                            ImGui::SetCursorScreenPos(
                                {center.x - st.popupSlotRadius, center.y - st.popupSlotRadius - 16.f});
                            ImGui::TextDisabled("[S] %s", shoutName);
                        } else if (rSp || lSp) {
                            const std::string label =
                                std::string(rSp ? rSp->GetName() : "---") + " | " + (lSp ? lSp->GetName() : "---");
                            ImGui::SetCursorScreenPos(
                                {center.x - st.popupSlotRadius, center.y - st.popupSlotRadius - 16.f});
                            ImGui::TextDisabled("%s", label.c_str());
                        }
                    }

                    {
                        const float dx = g_mousePos.x - center.x;
                        const float dy = g_mousePos.y - center.y;
                        const bool inCircle = (dx * dx + dy * dy) < (st.popupSlotRadius * st.popupSlotRadius);

                        if (inCircle) {
                            if (hovIsShoutOrPower) {
                                DL::AddCircleFilled(dl, center, st.popupSlotRadius - 1.f, IM_COL32(255, 200, 80, 40),
                                                    48);

                                ImGui::SetCursorScreenPos({g_mousePos.x + 14.f, g_mousePos.y + 4.f});
                                ImGui::TextDisabled("LMB assign  |  RMB clear  [Shout/Power]");

                                if (clicked) MagicAssign::TryAssignHoveredShoutToSlot(i);
                                if (rightClicked) {
                                    if (shoutID)
                                        MagicAssign::TryClearSlotShout(i);
                                    else {
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                    }
                                }
                            } else {
                                const bool hoverRight = dx >= 0.f;
                                const ImU32 hlColor = IM_COL32(255, 200, 80, 40);
                                if (hoverRight)
                                    FillSector(dl, center, st.popupSlotRadius - 1.f, -kPI * 0.5f, kPI * 0.5f, hlColor);
                                else
                                    FillSector(dl, center, st.popupSlotRadius - 1.f, kPI * 0.5f, kPI * 1.5f, hlColor);

                                const char* tip = hoverRight ? "LMB assign  |  RMB clear  [Right]"
                                                             : "LMB assign  |  RMB clear  [Left]";
                                ImGui::SetCursorScreenPos({g_mousePos.x + 14.f, g_mousePos.y + 4.f});
                                ImGui::TextDisabled("%s", tip);

                                if (clicked) {
                                    if (hoverRight)
                                        MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Right);
                                    else
                                        MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Left);
                                }
                                if (rightClicked) {
                                    if (shoutID) {
                                        MagicAssign::TryClearSlotShout(i);
                                    } else {
                                        if (hoverRight)
                                            MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                        else
                                            MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                    }
                                }
                            }
                        }
                    }

                    {
                        const float widgetY = center.y + st.popupSlotRadius + 4.f;
                        if (shoutID) {
                            DrawSpellModeWidget(dl, clicked, {center.x - st.modeWidgetW * 0.5f, widgetY},
                                                st.modeWidgetW, shoutID, std::to_string(i).append("S").c_str());
                        } else {
                            if (rID)
                                DrawSpellModeWidget(dl, clicked, {center.x + 2.f, widgetY}, st.modeWidgetW, rID,
                                                    std::to_string(i).append("R").c_str());
                            if (lID)
                                DrawSpellModeWidget(dl, clicked, {center.x - st.modeWidgetW - 2.f, widgetY},
                                                    st.modeWidgetW, lID, std::to_string(i).append("L").c_str());
                        }
                    }
                }

                const bool mouseOutside = clicked && (g_mousePos.x < popupPos.x || g_mousePos.x > popupEnd.x ||
                                                      g_mousePos.y < popupPos.y || g_mousePos.y > popupEnd.y);

                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || mouseOutside) {
                    if (g_popupWindow) g_popupWindow->IsOpen = false;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            DrawOverlayAndCursor({io->DisplaySize.x, io->DisplaySize.y}, g_mousePos);
        }
    }

    bool IsDetailPopupOpen() { return g_popupWindow && g_popupWindow->IsOpen.load(std::memory_order_relaxed); }

    void FeedMouseDelta(float dx, float dy) {
        auto const* io = ImGui::GetIO();
        g_mousePos.x = std::clamp(g_mousePos.x + dx, 0.f, io->DisplaySize.x);
        g_mousePos.y = std::clamp(g_mousePos.y + dy, 0.f, io->DisplaySize.y);
    }

    void FeedMouseClick() { g_mouseClicked.store(true, std::memory_order_relaxed); }
    void FeedMouseRightClick() { g_mouseRightClicked.store(true, std::memory_order_relaxed); }

    void ToggleDetailPopup() {
        if (!g_popupWindow) return;
        const bool willOpen = !g_popupWindow->IsOpen.load();
        g_popupWindow->IsOpen = willOpen;
        if (willOpen) g_popupJustOpened.store(true, std::memory_order_relaxed);
    }

    void CloseDetailPopup() {
        if (g_popupWindow) g_popupWindow->IsOpen = false;
    }

    void DrawHudElement() {
        if (!g_hudVisible.load(std::memory_order_relaxed)) return;
        if (IsHardBlocked()) return;
        if (Slots::GetSlotCount() == 0) return;

        auto* ui = RE::UI::GetSingleton();
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        const bool inMagicMenu = ui && ui->IsMenuOpen(magicMenu);

        if (inMagicMenu && Input::ConsumeHudToggle()) ToggleDetailPopup();

        if (!inMagicMenu && g_popupWindow && g_popupWindow->IsOpen.load()) g_popupWindow->IsOpen = false;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});

        if (!IsSoftBlocked()) DrawSmallHUD(ImGui::GetIO());

        ImGui::PopStyleVar(2);
    }

    void DrawWindowElement() {
        if (IsHardBlocked()) {
            if (g_popupWindow) g_popupWindow->IsOpen = false;
            return;
        }
        if (Slots::GetSlotCount() == 0) return;

        DrawDetailPopup();
    }

    bool IsHudVisible() { return g_hudVisible.load(std::memory_order_relaxed); }

    void SetHudVisible(bool visible) { g_hudVisible.store(visible, std::memory_order_relaxed); }

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) return;

        static auto const* hudElement = SKSEMenuFramework::AddHudElement(DrawHudElement);
        (void)hudElement;

        g_popupWindow = SKSEMenuFramework::AddWindow(DrawWindowElement, false);

        static auto const* closeEvent = SKSEMenuFramework::AddEvent(
            [](SKSEMenuFramework::Model::EventType type) {
                if (type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
                    g_mouseClicked.store(false, std::memory_order_relaxed);
                    g_mouseRightClicked.store(false, std::memory_order_relaxed);
                }
            },
            0.f);
        (void)closeEvent;
    }
}