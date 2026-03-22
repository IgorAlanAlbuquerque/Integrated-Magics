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
#include "Strings.h"
#include "UI/SlotAnimator.h"
#include "UI/SlotLayout.h"
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

                if (bgImg.valid()) {
                    const ImVec2 p0{center.x - r, center.y - r};
                    const ImVec2 p1{center.x + r, center.y + r};
                    DL::AddImage(dl, reinterpret_cast<ImTextureID>(bgImg.texture), p0, p1, {0.f, 0.f}, {1.f, 1.f},
                                 IM_COL32(255, 255, 255, 255));
                } else {
                    DL::AddCircleFilled(dl, center, r, isActive ? st.slotBgActive : st.slotBgInactive, 48);
                }
            } else {
                DL::AddCircleFilled(dl, center, r, isActive ? st.slotBgActive : st.slotBgInactive, 48);
            }

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
                DL::AddCircle(dl, center, r, ring, 48, st.slotRingWidth);
                DL::AddCircle(dl, center, r + st.slotRingWidth + 0.5f, (ring & 0x00FFFFFFu) | (70u << 24), 48, 1.0f);
            } else {
                DL::AddCircle(dl, center, r, st.slotRingInactive, 48, st.slotRingWidth * 0.5f);
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

        const TextureManager::Image& ResolveModifierIcon() {
            static const TextureManager::Image kEmpty{};
            const auto& cfg = IntegratedMagic::GetMagicConfig();
            const auto& st = StyleConfig::Get();

            const int kbPos = cfg.modifierKeyboardPosition;
            const int gpPos = cfg.modifierGamepadPosition;

            if (st.buttonIconType == ButtonIconType::Keyboard) {
                if (kbPos <= 0) return kEmpty;
                const auto& ic = cfg.slotInput[0];
                int scancode = -1;
                if (kbPos == 1)
                    scancode = ic.KeyboardScanCode1.load(std::memory_order_relaxed);
                else if (kbPos == 2)
                    scancode = ic.KeyboardScanCode2.load(std::memory_order_relaxed);
                else
                    scancode = ic.KeyboardScanCode3.load(std::memory_order_relaxed);
                if (scancode < 0) return kEmpty;
                return TextureManager::GetKeyboardIcon(scancode);
            } else {
                if (gpPos <= 0) return kEmpty;
                const auto& ic = cfg.slotInput[0];
                int buttonIndex = -1;
                if (gpPos == 1)
                    buttonIndex = ic.GamepadButton1.load(std::memory_order_relaxed);
                else if (gpPos == 2)
                    buttonIndex = ic.GamepadButton2.load(std::memory_order_relaxed);
                else
                    buttonIndex = ic.GamepadButton3.load(std::memory_order_relaxed);
                if (buttonIndex < 0) return kEmpty;
                return TextureManager::GetGamepadButtonIcon(buttonIndex, st.buttonIconType);
            }
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
            const ImVec2 pos = {c.x + st.modifierWidgetOffsetX, c.y + st.modifierWidgetOffsetY};
            const float iconSize = r * 2.f;
            const ImVec2 ip0 = {pos.x - iconSize * 0.5f, pos.y - iconSize * 0.5f};
            const ImVec2 ip1 = {pos.x + iconSize * 0.5f, pos.y + iconSize * 0.5f};

            DL::AddImage(dl, reinterpret_cast<ImTextureID>(icon.texture), ip0, ip1, {0.f, 0.f}, {1.f, 1.f},
                         IM_COL32(255, 255, 255, alpha));
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

        void MouseTooltip(const char* text) {
            ImGui::SetNextWindowPos({g_mousePos.x + 16.f, g_mousePos.y + 8.f}, ImGuiCond_Always, {0.f, 0.f});
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(text);
            ImGui::EndTooltip();
        }

        float DrawActionBadge(ImDrawList* dl, ImVec2 origin, float iconSize, const TextureManager::Image& icon,
                              const char* fallbackLabel, const char* actionLabel, ImU32 bgColor) {
            constexpr float kGap = 6.f;

            if (icon.valid()) {
                const ImVec2 ip0 = origin;
                const ImVec2 ip1 = {ip0.x + iconSize, ip0.y + iconSize};
                DL::AddImage(dl, reinterpret_cast<ImTextureID>(icon.texture), ip0, ip1, {0.f, 0.f}, {1.f, 1.f},
                             IM_COL32(255, 255, 255, 255));

                ImVec2 ts{};
                ImGui::CalcTextSize(&ts, actionLabel, nullptr, false, -1.f);
                ImGui::SetCursorScreenPos({ip1.x + kGap, ip0.y + (iconSize - ts.y) * 0.5f});
                ImGui::TextDisabled("%s", actionLabel);

                return iconSize + kGap + ts.x + kGap * 2.f;
            } else {
                constexpr float kPad = 4.f;
                const float chipW = iconSize + kPad * 2.f;
                const float chipH = iconSize + kPad * 2.f;
                const ImVec2 chipP0 = origin;
                const ImVec2 chipP1 = {chipP0.x + chipW, chipP0.y + chipH};
                DL::AddRectFilled(dl, chipP0, chipP1, bgColor, 4.f, 0);
                DL::AddRect(dl, chipP0, chipP1, IM_COL32(200, 200, 200, 80), 4.f, 0, 1.f);

                ImVec2 lts{};
                ImGui::CalcTextSize(&lts, fallbackLabel, nullptr, false, -1.f);
                ImGui::SetCursorScreenPos({chipP0.x + (chipW - lts.x) * 0.5f, chipP0.y + (chipH - lts.y) * 0.5f});
                ImGui::TextUnformatted(fallbackLabel);

                ImVec2 ts{};
                ImGui::CalcTextSize(&ts, actionLabel, nullptr, false, -1.f);
                ImGui::SetCursorScreenPos({chipP1.x + kGap, chipP0.y + (chipH - ts.y) * 0.5f});
                ImGui::TextDisabled("%s", actionLabel);

                return chipW + kGap + ts.x + kGap * 2.f;
            }
        }

        void DrawPopupActionHints(ImDrawList* dl, ImVec2 popupPos, ImVec2 popupSize, bool isShoutOrPower,
                                  bool hoverRight) {
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
                assignFallback = "LMB";
                clearFallback = "RMB";
            }

            const std::string assignText = isShoutOrPower
                                               ? Strings::Get("Popup_Hint_Assign", "Assign")
                                               : (hoverRight ? Strings::Get("Popup_Hint_AssignRight", "Assign Right")
                                                             : Strings::Get("Popup_Hint_AssignLeft", "Assign Left"));
            const std::string clearText = Strings::Get("Popup_Hint_Clear", "Clear");

            const float chipW = kIconSize;
            ImVec2 assignTS{}, clearTS{};
            ImGui::CalcTextSize(&assignTS, assignText.c_str(), nullptr, false, -1.f);
            ImGui::CalcTextSize(&clearTS, clearText.c_str(), nullptr, false, -1.f);
            const float totalW = chipW + 6.f + assignTS.x + 6.f * 2.f + kSpacing + chipW + 6.f + clearTS.x + 6.f * 2.f;
            const float barH = kIconSize;

            const ImVec2 barOrigin = {popupPos.x + (popupSize.x - totalW) * 0.5f,
                                      popupPos.y + popupSize.y - barH - kBarPad};

            const ImU32 assignBg = IM_COL32(40, 100, 200, 160);
            const ImU32 clearBg = IM_COL32(160, 40, 40, 160);

            float x = barOrigin.x;
            x += DrawActionBadge(dl, {x, barOrigin.y}, kIconSize, assignIcon ? *assignIcon : kEmpty, assignFallback,
                                 assignText.c_str(), assignBg);
            x += kSpacing;
            DrawActionBadge(dl, {x, barOrigin.y}, kIconSize, clearIcon ? *clearIcon : kEmpty, clearFallback,
                            clearText.c_str(), clearBg);
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
                    MouseTooltip(kTips[m]);
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

        ImVec2 ComputeHudCenter(const ImGuiIO* io, ImVec2 halfSize) {
            const float W = io->DisplaySize.x;
            const float H = io->DisplaySize.y;
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
                int codes[3] = {
                    ic.KeyboardScanCode1.load(std::memory_order_relaxed),
                    ic.KeyboardScanCode2.load(std::memory_order_relaxed),
                    ic.KeyboardScanCode3.load(std::memory_order_relaxed),
                };
                for (int c : codes)
                    if (c >= 0 && keyCount < 3) keys[keyCount++] = {false, c};
            } else {
                int codes[3] = {
                    ic.GamepadButton1.load(std::memory_order_relaxed),
                    ic.GamepadButton2.load(std::memory_order_relaxed),
                    ic.GamepadButton3.load(std::memory_order_relaxed),
                };
                for (int c : codes)
                    if (c >= 0 && keyCount < 3) keys[keyCount++] = {true, c};
            }

            if (keyCount == 0) return;

            const TextureManager::Image* imgs[3]{nullptr, nullptr, nullptr};
            int validCount = 0;
            for (int k = 0; k < keyCount; ++k) {
                const auto& img = keys[k].isGamepad ? TextureManager::GetGamepadButtonIcon(keys[k].code, iconType)
                                                    : TextureManager::GetKeyboardIcon(keys[k].code);
                if (img.valid()) {
                    imgs[validCount++] = &img;
                }
            }
            if (validCount == 0) return;

            const float totalW = validCount * kIconSize + (validCount - 1) * kSpacing;

            const float startX = center.x - totalW * 0.5f;
            const float startY = center.y - slotR - kMarginY - kIconSize;

            for (int k = 0; k < validCount; ++k) {
                const float x = startX + k * (kIconSize + kSpacing);
                const ImVec2 ip0 = {x, startY};
                const ImVec2 ip1 = {x + kIconSize, startY + kIconSize};
                DL::AddImage(dl, reinterpret_cast<ImTextureID>(imgs[k]->texture), ip0, ip1, {0.f, 0.f}, {1.f, 1.f},
                             IM_COL32(255, 255, 255, 255));
            }
        }

        void DrawSlotButtonLabel(ImDrawList* dl, ImVec2 center, float slotR, int slotIndex, ImVec2 hudOrigin,
                                 float alpha) {
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
                int codes[3] = {
                    ic.KeyboardScanCode1.load(std::memory_order_relaxed),
                    ic.KeyboardScanCode2.load(std::memory_order_relaxed),
                    ic.KeyboardScanCode3.load(std::memory_order_relaxed),
                };
                for (int k = 0; k < 3; ++k) {
                    if (codes[k] < 0) continue;
                    if (suppressMod && (k + 1) == modPos) continue;
                    if (keyCount < 3) keys[keyCount++] = {false, codes[k]};
                }
            } else {
                int codes[3] = {
                    ic.GamepadButton1.load(std::memory_order_relaxed),
                    ic.GamepadButton2.load(std::memory_order_relaxed),
                    ic.GamepadButton3.load(std::memory_order_relaxed),
                };
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
                    const float dx = hudOrigin.x - center.x;
                    const float dy = hudOrigin.y - center.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 0.5f) {
                        const float nx = dx / len, ny = dy / len;
                        const float ax = center.x + nx * (slotR + margin + iconSize * 0.5f);
                        const float ay = center.y + ny * (slotR + margin + iconSize * 0.5f);
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
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 0.5f) {
                        const float nx = dx / len, ny = dy / len;
                        const float ax = center.x + nx * (slotR + margin + iconSize * 0.5f);
                        const float ay = center.y + ny * (slotR + margin + iconSize * 0.5f);
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
                DL::AddImage(dl, reinterpret_cast<ImTextureID>(imgs[k]->texture), {x, startY},
                             {x + iconSize, startY + iconSize}, {0.f, 0.f}, {1.f, 1.f}, tint);
            }
        }

        void DrawSmallHUD(ImGuiIO const* io) {
            const auto& st = Style();
            const int n = static_cast<int>(Slots::GetSlotCount());
            const int activeSlot = MagicState::Get().ActiveSlot();

            const bool modHeld = !MagicState::Get().IsActive() && Input::IsModifierHeld();
            SlotAnimator::Update(n, activeSlot, modHeld, st.hudLayout, st.gridColumns);

            static float s_labelAlpha[SlotLayout::kMaxSlots]{};
            {
                using clock = std::chrono::steady_clock;
                static clock::time_point s_lastLabel = clock::now();
                const auto now = clock::now();
                float dt = std::chrono::duration<float>(now - s_lastLabel).count();
                s_lastLabel = now;
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
                    const float diff = target - s_labelAlpha[i];
                    s_labelAlpha[i] += diff * std::min(dt * fadeSpeed, 1.f);
                    s_labelAlpha[i] = std::clamp(s_labelAlpha[i], 0.f, 1.f);
                }

                for (int i = n; i < SlotLayout::kMaxSlots; ++i) s_labelAlpha[i] = 0.f;
            }

            const LayoutVec2 fixedHalf = [&] {
                LayoutVec2 h = SlotLayout::BoundingHalf(st.hudLayout, n, st.slotRadius, st.ringRadius, st.slotSpacing,
                                                        st.gridColumns);
                return LayoutVec2{h.x + kGlowPad, h.y + kGlowPad};
            }();

            float maxScale = SlotAnimator::MaxPossibleScale();
            for (int i = 0; i < n; ++i) maxScale = std::max(maxScale, SlotAnimator::GetScale(i));

            const LayoutVec2 animHalf = [&] {
                LayoutVec2 h = SlotLayout::BoundingHalf(st.hudLayout, n, st.slotRadius * maxScale, st.ringRadius,
                                                        st.slotSpacing, st.gridColumns);
                return LayoutVec2{h.x + kGlowPad, h.y + kGlowPad};
            }();

            const ImVec2 hudOrigin = ComputeHudCenter(io, {fixedHalf.x, fixedHalf.y});

            LayoutVec2 relPos[SlotLayout::kMaxSlots]{};
            SlotLayout::Compute(st.hudLayout, n, st.slotRadius, st.ringRadius, st.slotSpacing, st.gridColumns, relPos);

            auto ScaledCenter = [&](int idx) -> ImVec2 {
                const float scale = SlotAnimator::GetScale(idx);
                const float rx = relPos[idx].x;
                const float ry = relPos[idx].y;
                const float len = std::sqrt(rx * rx + ry * ry);
                const float push = (scale - 1.f) * st.slotRadius;
                if (len > 0.5f) {
                    return {hudOrigin.x + rx + (rx / len) * push, hudOrigin.y + ry + (ry / len) * push};
                }
                return {hudOrigin.x + rx, hudOrigin.y + ry};
            };

            ImGui::SetNextWindowPos({hudOrigin.x - animHalf.x, hudOrigin.y - animHalf.y}, ImGuiCond_Always, {0.f, 0.f});
            ImGui::SetNextWindowSize({animHalf.x * 2.f, animHalf.y * 2.f}, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.f);

            ImGui::Begin(kHudWindowID, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoInputs);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (SlotLayout::HasCenter(st.hudLayout)) DrawRingCenter(dl, hudOrigin);

            for (int i = 0; i < n; ++i) {
                if (i == activeSlot) continue;
                const ImVec2 center = ScaledCenter(i);
                const float slotR = st.slotRadius * SlotAnimator::GetScale(i);
                const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
                const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
                const auto shoutID = Slots::GetSlotShout(i);
                auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
                const bool isTwoHanded = !shoutID && !rID && lSp && MagicAssign::IsTwoHandedSpell(lSp);
                DrawSlotVisual(dl, center, slotR, false, isTwoHanded ? nullptr : rSp, isTwoHanded ? nullptr : lSp,
                               isTwoHanded ? lID : shoutID);
            }

            if (activeSlot >= 0 && activeSlot < n) {
                const ImVec2 center = ScaledCenter(activeSlot);
                const float slotR = st.slotRadius * SlotAnimator::GetScale(activeSlot);
                const auto rID = Slots::GetSlotSpell(activeSlot, Slots::Hand::Right);
                const auto lID = Slots::GetSlotSpell(activeSlot, Slots::Hand::Left);
                const auto shoutID = Slots::GetSlotShout(activeSlot);
                auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
                const bool isTwoHanded = !shoutID && !rID && lSp && MagicAssign::IsTwoHandedSpell(lSp);
                DrawSlotVisual(dl, center, slotR, true, isTwoHanded ? nullptr : rSp, isTwoHanded ? nullptr : lSp,
                               isTwoHanded ? lID : shoutID);
            }

            for (int i = 0; i < n; ++i) {
                if (i == activeSlot) continue;
                DrawSlotButtonLabel(dl, ScaledCenter(i), st.slotRadius * SlotAnimator::GetScale(i), i, hudOrigin,
                                    s_labelAlpha[i]);
            }
            if (activeSlot >= 0 && activeSlot < n) {
                DrawSlotButtonLabel(dl, ScaledCenter(activeSlot), st.slotRadius * SlotAnimator::GetScale(activeSlot),
                                    activeSlot, hudOrigin, s_labelAlpha[activeSlot]);
            }

            const bool modWidgetHeld = Input::IsModifierHeld() || MagicState::Get().IsActive();
            DrawModifierWidget(dl, hudOrigin, modWidgetHeld);

            ImGui::End();
        }

        void DrawDetailPopup() {
            ImGuiIO const* io = ImGui::GetIO();

            if (g_popupJustOpened.exchange(false)) {
                g_mousePos = {io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f};
            }

            const bool clicked = g_mouseClicked.exchange(false, std::memory_order_relaxed);
            const bool rightClicked = g_mouseRightClicked.exchange(false, std::memory_order_relaxed);

            bool g_hintsVisible = false;
            bool g_hintsShout = false;
            bool g_hintsHoverRight = false;

            const int n = static_cast<int>(Slots::GetSlotCount());
            const auto& st = Style();
            const float dynPopupR = DynamicRingRadius(n, st.popupSlotRadius, st.popupRingRadius, st.popupSlotGap);

            const LayoutVec2 bh = SlotLayout::BoundingHalf(st.popupLayout, n, st.popupSlotRadius, dynPopupR,
                                                           st.popupSlotGap, st.gridColumns);
            const float popupHalfX = bh.x + kGlowPad + st.modeWidgetW + 12.f;
            const float popupHalfY = bh.y + kGlowPad + st.modeWidgetW + 12.f;
            const ImVec2 popupSize = {popupHalfX * 2.f, popupHalfY * 2.f + 48.f};
            const ImVec2 popupPos = {io->DisplaySize.x * 0.5f - popupSize.x * 0.5f,
                                     io->DisplaySize.y * 0.5f - popupSize.y * 0.5f};
            const ImVec2 popupEnd = {popupPos.x + popupSize.x, popupPos.y + popupSize.y};
            const ImVec2 ringCenter = {io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f};

            LayoutVec2 popupRelPos[SlotLayout::kMaxSlots]{};
            SlotLayout::Compute(st.popupLayout, n, st.popupSlotRadius, dynPopupR, st.popupSlotGap, st.gridColumns,
                                popupRelPos);

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

                const auto hovType = MagicAssign::GetHoveredMagicType();
                const bool hovIsShoutOrPower =
                    hovType == MagicAssign::HoveredMagicType::Shout || hovType == MagicAssign::HoveredMagicType::Power;
                const bool hovIsTwoHanded = hovType == MagicAssign::HoveredMagicType::TwoHandedSpell;

                const bool hovIsFullSlot = hovIsShoutOrPower || hovIsTwoHanded;

                for (int i = 0; i < n; ++i) {
                    const ImVec2 center = {ringCenter.x + popupRelPos[i].x, ringCenter.y + popupRelPos[i].y};
                    const auto rID = Slots::GetSlotSpell(i, Slots::Hand::Right);
                    const auto lID = Slots::GetSlotSpell(i, Slots::Hand::Left);
                    const auto shoutID = Slots::GetSlotShout(i);
                    auto const* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
                    auto const* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;

                    const bool slotIsTwoHanded = !shoutID && !rID && lSp && MagicAssign::IsTwoHandedSpell(lSp);
                    const RE::FormID displayShoutID = shoutID ? shoutID : (slotIsTwoHanded ? lID : 0);
                    const RE::SpellItem* displayL = slotIsTwoHanded ? nullptr : lSp;
                    const RE::SpellItem* displayR = slotIsTwoHanded ? nullptr : rSp;

                    DrawSlotVisual(dl, center, st.popupSlotRadius, activeSlot == i, displayR, displayL, displayShoutID);

                    {
                        DrawSlotHotkeyIcons(dl, center, st.popupSlotRadius, i);
                    }

                    {
                        if (shoutID || slotIsTwoHanded) {
                            auto* displayForm = RE::TESForm::LookupByID(displayShoutID);
                            const char* displayName = displayForm ? displayForm->GetName() : "???";
                            const char* prefix = shoutID ? Strings::Get("Popup_ShoutPrefix", "[S]").c_str() : "[2H]";
                            ImGui::SetCursorScreenPos(
                                {center.x - st.popupSlotRadius, center.y - st.popupSlotRadius - 16.f});
                            ImGui::TextDisabled("%s %s", prefix, displayName);
                        } else if (rSp || lSp) {
                            const std::string label =
                                std::string(lSp ? lSp->GetName() : "---") + " | " + (rSp ? rSp->GetName() : "---");
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
                            if (hovIsFullSlot) {
                                DL::AddCircleFilled(dl, center, st.popupSlotRadius - 1.f, IM_COL32(255, 200, 80, 40),
                                                    48);

                                if (clicked) {
                                    if (hovIsTwoHanded)

                                        MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Left);
                                    else
                                        MagicAssign::TryAssignHoveredShoutToSlot(i);
                                }
                                if (rightClicked) {
                                    if (shoutID)
                                        MagicAssign::TryClearSlotShout(i);
                                    else {
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                    }
                                }

                                g_hintsVisible = true;
                                g_hintsShout = true;
                                g_hintsHoverRight = false;
                            } else {
                                const bool hoverRight = dx >= 0.f;
                                const ImU32 hlColor = IM_COL32(255, 200, 80, 40);
                                if (hoverRight)
                                    FillSector(dl, center, st.popupSlotRadius - 1.f, -kPI * 0.5f, kPI * 0.5f, hlColor);
                                else
                                    FillSector(dl, center, st.popupSlotRadius - 1.f, kPI * 0.5f, kPI * 1.5f, hlColor);

                                if (clicked) {
                                    if (hoverRight)
                                        MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Right);
                                    else
                                        MagicAssign::TryAssignHoveredSpellToSlot(i, Slots::Hand::Left);
                                }
                                if (rightClicked) {
                                    if (shoutID) {
                                        MagicAssign::TryClearSlotShout(i);
                                    } else if (slotIsTwoHanded) {
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                        MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                    } else {
                                        if (hoverRight)
                                            MagicAssign::TryClearSlotHand(i, Slots::Hand::Right);
                                        else
                                            MagicAssign::TryClearSlotHand(i, Slots::Hand::Left);
                                    }
                                }

                                g_hintsVisible = true;
                                g_hintsShout = false;
                                g_hintsHoverRight = hoverRight;
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

                if (g_hintsVisible) DrawPopupActionHints(dl, popupPos, popupSize, g_hintsShout, g_hintsHoverRight);

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
        if (IsHardBlocked()) return;
        if (Slots::GetSlotCount() == 0) return;

        auto* ui = RE::UI::GetSingleton();
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        const bool inMagicMenu = ui && ui->IsMenuOpen(magicMenu);

        if (inMagicMenu && Input::ConsumeHudToggle()) ToggleDetailPopup();
        if (!inMagicMenu && g_popupWindow && g_popupWindow->IsOpen.load()) g_popupWindow->IsOpen = false;

        if (!g_hudVisible.load(std::memory_order_relaxed)) return;
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