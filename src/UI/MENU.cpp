#include "MENU.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>

#include "Config/Config.h"
#include "Input/Input.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "SKSEMenuFramework.h"
#include "Strings.h"
#include "UI/HUD.h"
#include "UI/StyleConfig.h"

namespace ImGui = ImGuiMCP;

namespace {
    struct FieldCaptureState {
        std::atomic<int>* field{nullptr};
        bool wantKeyboard{true};
        bool active{false};
    };

    bool g_pending = false;
    constexpr int kHudPopupSlot = -1;
    int g_selectedSlot = 0;
    FieldCaptureState g_fieldCapture{};

    inline int ClampMinusOne(int v) { return (v < -1) ? -1 : v; }

    inline void DrawAtomicIntInput(const std::string& label, std::atomic<int>& atom, bool& dirty,
                                   float width = 150.0f) {
        int v = atom.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputInt(label.c_str(), &v)) {
            v = ClampMinusOne(v);
            atom.store(v, std::memory_order_relaxed);
            dirty = true;
        }
    }

    void ClearSlotData(IntegratedMagic::MagicConfig& cfg, int slot) {
        const auto idx = static_cast<std::size_t>(slot);
        cfg.slotSpellFormIDLeft[idx].store(0u, std::memory_order_relaxed);
        cfg.slotSpellFormIDRight[idx].store(0u, std::memory_order_relaxed);
        cfg.slotShoutFormID[idx].store(0u, std::memory_order_relaxed);
        auto& icfg = cfg.slotInput[idx];
        icfg.KeyboardScanCode1.store(-1, std::memory_order_relaxed);
        icfg.KeyboardScanCode2.store(-1, std::memory_order_relaxed);
        icfg.KeyboardScanCode3.store(-1, std::memory_order_relaxed);
        icfg.GamepadButton1.store(-1, std::memory_order_relaxed);
        icfg.GamepadButton2.store(-1, std::memory_order_relaxed);
        icfg.GamepadButton3.store(-1, std::memory_order_relaxed);
    }

    bool SlotHasHotkey(const IntegratedMagic::InputConfig& icfg) {
        return icfg.KeyboardScanCode1.load(std::memory_order_relaxed) != -1 ||
               icfg.GamepadButton1.load(std::memory_order_relaxed) != -1;
    }

    void CancelFieldCapture() {
        if (g_fieldCapture.active) {
            Input::CancelHotkeyCapture();
            g_fieldCapture = {};
        }
    }

    void DrawKeyValueCapture(std::atomic<int>& field, bool wantKeyboard, bool& dirty, int rowPos = 0,
                             IntegratedMagic::MagicConfig* cfg = nullptr) {
        ImGui::PushID(static_cast<void*>(&field));

        ImGui::SetNextItemWidth(70.0f);
        if (int v = field.load(std::memory_order_relaxed); ImGui::InputInt("##v", &v, 0, 0)) {
            v = ClampMinusOne(v);
            field.store(v, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SameLine();

        if (const bool isThis = g_fieldCapture.active && g_fieldCapture.field == &field; isThis) {
            if (const int encoded = Input::PollCapturedHotkey(); encoded != -1) {
                const bool gotKb = (encoded >= 0);
                if (gotKb == wantKeyboard) {
                    const int val = wantKeyboard ? encoded : -(encoded + 1);
                    field.store(val, std::memory_order_relaxed);
                    dirty = true;
                    g_fieldCapture = {};
                } else {
                    Input::RequestHotkeyCapture();
                }
            }

            ImGui::TextDisabled("...");
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                CancelFieldCapture();
            }
        } else {
            if (g_fieldCapture.active) ImGui::BeginDisabled(true);
            if (ImGui::SmallButton(IntegratedMagic::Strings::Get("Btn_Cap", "Cap").c_str())) {
                g_fieldCapture = {&field, wantKeyboard, true};
                Input::RequestHotkeyCapture();
            }
            if (g_fieldCapture.active) ImGui::EndDisabled();
        }

        if (cfg && rowPos > 0) {
            ImGui::SameLine();
            int& modPos = wantKeyboard ? cfg->modifierKeyboardPosition : cfg->modifierGamepadPosition;
            const bool isMod = (modPos == rowPos);
            if (isMod) ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
            ImGui::PushID(wantKeyboard ? "kbm" : "gpm");
            if (ImGui::SmallButton("M")) {
                if (isMod) {
                    modPos = 0;
                } else {
                    modPos = rowPos;

                    const int val = field.load(std::memory_order_relaxed);
                    const auto n = cfg->SlotCount();
                    for (std::uint32_t i = 0; i < n; ++i) {
                        auto& ic = cfg->slotInput[i];
                        std::atomic<int>* target = nullptr;
                        if (wantKeyboard) {
                            if (rowPos == 1)
                                target = &ic.KeyboardScanCode1;
                            else if (rowPos == 2)
                                target = &ic.KeyboardScanCode2;
                            else
                                target = &ic.KeyboardScanCode3;
                        } else {
                            if (rowPos == 1)
                                target = &ic.GamepadButton1;
                            else if (rowPos == 2)
                                target = &ic.GamepadButton2;
                            else
                                target = &ic.GamepadButton3;
                        }
                        target->store(val, std::memory_order_relaxed);
                    }
                }
                dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(isMod ? "Unmark as modifier" : "Mark as modifier");
            ImGui::PopID();
            if (isMod) ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    void DrawInputDetailRow(const char* kbLabel, std::atomic<int>& kbField, const char* gpLabel,
                            std::atomic<int>& gpField, bool& dirty, int rowPos, IntegratedMagic::MagicConfig& cfg) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(kbLabel);

        ImGui::TableSetColumnIndex(1);
        {
            const bool dirtyBefore = dirty;
            DrawKeyValueCapture(kbField, true, dirty, rowPos, &cfg);
            if (dirty && !dirtyBefore && cfg.modifierKeyboardPosition == rowPos) {
                const int val = kbField.load(std::memory_order_relaxed);
                const auto n = cfg.SlotCount();
                for (std::uint32_t i = 0; i < n; ++i) {
                    auto& ic = cfg.slotInput[i];
                    auto* field = rowPos == 1   ? &ic.KeyboardScanCode1
                                  : rowPos == 2 ? &ic.KeyboardScanCode2
                                                : &ic.KeyboardScanCode3;
                    field->store(val, std::memory_order_relaxed);
                }
            }
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(gpLabel);

        ImGui::TableSetColumnIndex(3);
        {
            const bool dirtyBefore = dirty;
            DrawKeyValueCapture(gpField, false, dirty, rowPos, &cfg);
            if (dirty && !dirtyBefore && cfg.modifierGamepadPosition == rowPos) {
                const int val = gpField.load(std::memory_order_relaxed);
                const auto n = cfg.SlotCount();
                for (std::uint32_t i = 0; i < n; ++i) {
                    auto& ic = cfg.slotInput[i];
                    auto* field = rowPos == 1   ? &ic.GamepadButton1
                                  : rowPos == 2 ? &ic.GamepadButton2
                                                : &ic.GamepadButton3;
                    field->store(val, std::memory_order_relaxed);
                }
            }
        }
    }

    void DrawDetailPanel(IntegratedMagic::InputConfig& icfg, const char* title, bool& dirty,
                         IntegratedMagic::MagicConfig& cfg, bool showModifier = true) {
        ImGui::TextUnformatted(title);
        ImGui::Separator();
        ImGui::Spacing();

        if (const ImGuiMCP::ImGuiTableFlags kTableFlags =
                ImGuiMCP::ImGuiTableFlags_BordersOuter | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_SizingFixedFit;
            ImGui::BeginTable("##InputDetail", 4, kTableFlags)) {
            ImGui::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Keyboard", "Keyboard").c_str(),
                                    ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("##kbv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Gamepad", "Gamepad").c_str(),
                                    ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("##gpv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableHeadersRow();

            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key1", "Key 1").c_str(), icfg.KeyboardScanCode1,
                               IntegratedMagic::Strings::Get("Item_Btn1", "Btn 1").c_str(), icfg.GamepadButton1, dirty,
                               showModifier ? 1 : 0, cfg);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key2", "Key 2").c_str(), icfg.KeyboardScanCode2,
                               IntegratedMagic::Strings::Get("Item_Btn2", "Btn 2").c_str(), icfg.GamepadButton2, dirty,
                               showModifier ? 2 : 0, cfg);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key3", "Key 3").c_str(), icfg.KeyboardScanCode3,
                               IntegratedMagic::Strings::Get("Item_Btn3", "Btn 3").c_str(), icfg.GamepadButton3, dirty,
                               showModifier ? 3 : 0, cfg);

            ImGui::EndTable();
        }

        if (g_fieldCapture.active) {
            ImGui::Spacing();
            const auto msg =
                std::format("{}...", g_fieldCapture.wantKeyboard
                                         ? IntegratedMagic::Strings::Get("Capture_WaitKb", "Press a keyboard key")
                                         : IntegratedMagic::Strings::Get("Capture_WaitGp", "Press a gamepad button"));
            ImGui::TextDisabled("%s", msg.c_str());
        }
    }

    void DrawGeneralTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        ImGui::Spacing();

        const auto oldCount = static_cast<int>(cfg.SlotCount());
        int n = oldCount;
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_SlotCount", "Slot count").c_str(), &n)) {
            if (n < 1) n = 1;
            if (n > static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots)) {
                n = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
            }
            cfg.slotCount.store(static_cast<std::uint32_t>(n), std::memory_order_relaxed);
            dirty = true;
            if (n < oldCount) {
                for (int slot = n; slot < oldCount; ++slot) {
                    ClearSlotData(cfg, slot);
                }

                if (g_selectedSlot >= n) {
                    CancelFieldCapture();
                    g_selectedSlot = n > 0 ? 0 : kHudPopupSlot;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        bool hudVisible = IntegratedMagic::HUD::IsHudVisible();
        if (ImGui::Checkbox(IntegratedMagic::Strings::Get("Item_ShowHud", "Show HUD").c_str(), &hudVisible)) {
            IntegratedMagic::HUD::SetHudVisible(hudVisible);
            cfg.hudVisible = hudVisible;
            dirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            auto& st = IntegratedMagic::StyleConfig::Get();
            const std::string iconTypeNames =
                IntegratedMagic::Strings::Get("Item_ButtonIcon_Keyboard", "Keyboard") + '\0' +
                IntegratedMagic::Strings::Get("Item_ButtonIcon_PlayStation", "PlayStation") + '\0' +
                IntegratedMagic::Strings::Get("Item_ButtonIcon_Xbox", "Xbox") + '\0';
            int iconTypeIdx = static_cast<int>(st.buttonIconType);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo(IntegratedMagic::Strings::Get("Item_ButtonIconType", "Button icons##buttonicons").c_str(),
                             &iconTypeIdx, iconTypeNames.c_str())) {
                st.buttonIconType = static_cast<IntegratedMagic::ButtonIconType>(iconTypeIdx);
                dirty = true;
            }
        }
    }

    void DrawControlsTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        const auto n = static_cast<int>(cfg.SlotCount());

        if (g_selectedSlot >= n) {
            CancelFieldCapture();
            g_selectedSlot = n > 0 ? 0 : kHudPopupSlot;
        }

        ImGui::BeginChild("##CtrlList", ImGui::ImVec2{145.0f, 0.0f}, true);

        for (int slot = 0; slot < n; ++slot) {
            const auto& icfg = cfg.slotInput[static_cast<std::size_t>(slot)];
            const bool hasKey = SlotHasHotkey(icfg);

            const auto label = std::format(
                "{}  {}",
                IntegratedMagic::Strings::Get(std::format("List_Magic{}", slot + 1), std::format("Magic {}", slot + 1)),
                hasKey ? "*" : "-");

            if (ImGui::Selectable(label.c_str(), g_selectedSlot == slot) && (g_selectedSlot != slot)) {
                CancelFieldCapture();
                g_selectedSlot = slot;
            }
        }

        ImGui::Separator();

        {
            const bool hasKey = SlotHasHotkey(cfg.hudPopupInput);
            const auto label =
                std::format("{}  {}", IntegratedMagic::Strings::Get("List_HudPopup", "HUD Popup"), hasKey ? "*" : "-");
            if (ImGui::Selectable(label.c_str(), g_selectedSlot == kHudPopupSlot) &&
                (g_selectedSlot != kHudPopupSlot)) {
                CancelFieldCapture();
                g_selectedSlot = kHudPopupSlot;
            }
        }

        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##CtrlDetail", ImGui::ImVec2{0.0f, 0.0f}, true);

        if (g_selectedSlot == kHudPopupSlot) {
            DrawDetailPanel(cfg.hudPopupInput, IntegratedMagic::Strings::Get("Detail_HudPopup", "HUD Popup").c_str(),
                            dirty, cfg, false);
        } else if (g_selectedSlot >= 0 && g_selectedSlot < n) {
            const auto title = IntegratedMagic::Strings::Get(std::format("Detail_Magic{}", g_selectedSlot + 1),
                                                             std::format("Magic {}", g_selectedSlot + 1));
            DrawDetailPanel(cfg.slotInput[static_cast<std::size_t>(g_selectedSlot)], title.c_str(), dirty, cfg);
        }

        ImGui::EndChild();
    }

    void DrawAnchorWidget(bool& dirty) {
        using Anchor = IntegratedMagic::HudAnchor;
        struct AnchorCell {
            Anchor anchor;
            const char* label;
        };
        static constexpr AnchorCell kGrid[3][3] = {
            {{Anchor::TopLeft, "##TL"}, {Anchor::TopCenter, "##TC"}, {Anchor::TopRight, "##TR"}},
            {{Anchor::MiddleLeft, "##ML"}, {Anchor::Center, "##CC"}, {Anchor::MiddleRight, "##MR"}},
            {{Anchor::BottomLeft, "##BL"}, {Anchor::BottomCenter, "##BC"}, {Anchor::BottomRight, "##BR"}},
        };
        auto& st = IntegratedMagic::StyleConfig::Get();
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (col > 0) ImGui::SameLine(0.f, 2.f);
                const auto& cell = kGrid[row][col];
                const bool active = (st.hudAnchor == cell.anchor);
                if (active) ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
                if (ImGui::Button(cell.label, {28.f, 20.f})) {
                    st.hudAnchor = cell.anchor;
                    dirty = true;
                }
                if (active) ImGui::PopStyleColor();
            }
        }
    }

    void DrawHudTab(bool& dirty) {
        namespace S = IntegratedMagic::Strings;
        auto& st = IntegratedMagic::StyleConfig::Get();

        ImGui::SeparatorText(S::Get("HUD_Section_Layout", "Layout").c_str());
        ImGui::Spacing();

        {
            using LT = IntegratedMagic::HudLayoutType;
            const std::string layoutNames =
                S::Get("HUD_Layout_Circular", "Circular") + '\0' + S::Get("HUD_Layout_Horizontal", "Horizontal") +
                '\0' + S::Get("HUD_Layout_Vertical", "Vertical") + '\0' + S::Get("HUD_Layout_Grid", "Grid") + '\0';
            int layoutIdx = static_cast<int>(st.hudLayout);
            ImGui::SetNextItemWidth(150.f);
            if (ImGui::Combo(S::Get("HUD_Layout_Label", "Layout##hudlayout").c_str(), &layoutIdx,
                             layoutNames.c_str())) {
                st.hudLayout = static_cast<LT>(layoutIdx);
                dirty = true;
            }

            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.f);
                float spacing = st.slotSpacing;
                if (ImGui::InputFloat(S::Get("HUD_Spacing_Label", "Spacing##slotspacing").c_str(), &spacing, 1.f, 5.f,
                                      "%.1f")) {
                    st.slotSpacing = spacing;
                    dirty = true;
                }
            }
            if (st.hudLayout == LT::Grid) {
                ImGui::SetNextItemWidth(150.f);
                int cols = st.gridColumns;
                if (ImGui::InputInt(S::Get("HUD_Columns_Label", "Columns##gridcols").c_str(), &cols, 1, 1)) {
                    st.gridColumns = std::max(1, cols);
                    dirty = true;
                }
            }
            if (st.hudLayout == LT::Circular) {
                ImGui::SetNextItemWidth(150.f);
                float rr = st.ringRadius;
                if (ImGui::InputFloat(S::Get("HUD_RingRadius_Label", "Ring R##ringradius").c_str(), &rr, 1.f, 5.f,
                                      "%.1f")) {
                    st.ringRadius = rr;
                    dirty = true;
                }
            }
        }

        ImGui::Spacing();

        ImGui::SeparatorText(S::Get("HUD_Section_Position", "Position").c_str());
        ImGui::Spacing();

        DrawAnchorWidget(dirty);

        ImGui::SameLine(0.f, 16.f);
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(150.f);
        float ox = st.hudOffsetX;
        if (ImGui::InputFloat(S::Get("HUD_OffsetX_Label", "X##hudox").c_str(), &ox, 1.f, 5.f, "%.0f")) {
            st.hudOffsetX = ox;
            dirty = true;
        }
        ImGui::SetNextItemWidth(150.f);
        float oy = st.hudOffsetY;
        if (ImGui::InputFloat(S::Get("HUD_OffsetY_Label", "Y##hudoy").c_str(), &oy, 1.f, 5.f, "%.0f")) {
            st.hudOffsetY = oy;
            dirty = true;
        }
        ImGui::EndGroup();

        ImGui::Spacing();

        ImGui::SeparatorText(S::Get("HUD_Section_Slots", "Slots").c_str());
        ImGui::Spacing();

        {
            ImGui::SetNextItemWidth(150.f);
            float sr = st.slotRadius;
            if (ImGui::InputFloat(S::Get("HUD_SlotRadius_Label", "Radius##slotradius").c_str(), &sr, 1.f, 5.f,
                                  "%.1f")) {
                st.slotRadius = sr;
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float rw = st.slotRingWidth;
            if (ImGui::InputFloat(S::Get("HUD_RingWidth_Label", "Ring Width##ringwidth").c_str(), &rw, 0.5f, 1.f,
                                  "%.1f")) {
                st.slotRingWidth = std::max(0.f, rw);
                dirty = true;
            }
            ImGui::Spacing();

            ImGui::SetNextItemWidth(150.f);
            float as = st.slotActiveScale;
            if (ImGui::InputFloat(S::Get("HUD_ActiveScale_Label", "Active##activescale").c_str(), &as, 0.05f, 0.1f,
                                  "%.2f")) {
                st.slotActiveScale = as;
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float ms = st.slotModifierScale;
            if (ImGui::InputFloat(S::Get("HUD_ModifierScale_Label", "Modifier##modscale").c_str(), &ms, 0.05f, 0.1f,
                                  "%.2f")) {
                st.slotModifierScale = ms;
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float ns = st.slotNeighborScale;
            if (ImGui::InputFloat(S::Get("HUD_NeighborScale_Label", "Neighbor##neighborscale").c_str(), &ns, 0.05f,
                                  0.1f, "%.2f")) {
                st.slotNeighborScale = ns;
                dirty = true;
            }
            ImGui::Spacing();

            ImGui::SetNextItemWidth(150.f);
            float et = st.slotExpandTime;
            if (ImGui::InputFloat(S::Get("HUD_ExpandTime_Label", "Expand##expandtime").c_str(), &et, 0.01f, 0.05f,
                                  "%.2f")) {
                st.slotExpandTime = std::max(0.f, et);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float rt = st.slotRetractTime;
            if (ImGui::InputFloat(S::Get("HUD_RetractTime_Label", "Retract##retracttime").c_str(), &rt, 0.01f, 0.05f,
                                  "%.2f")) {
                st.slotRetractTime = std::max(0.f, rt);
                dirty = true;
            }
        }

        ImGui::Spacing();

        ImGui::SeparatorText(S::Get("HUD_Section_Icons", "Icons").c_str());
        ImGui::Spacing();

        {
            ImGui::SetNextItemWidth(150.f);
            float isf = st.iconSizeFactor;
            if (ImGui::InputFloat(S::Get("HUD_IconSize_Label", "Size##iconsizefactor").c_str(), &isf, 0.05f, 0.1f,
                                  "%.2f")) {
                st.iconSizeFactor = std::clamp(isf, 0.1f, 2.f);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float iof = st.iconOffsetFactor;
            if (ImGui::InputFloat(S::Get("HUD_IconOffset_Label", "Offset##iconoffsetfactor").c_str(), &iof, 0.05f, 0.1f,
                                  "%.2f")) {
                st.iconOffsetFactor = std::clamp(iof, 0.f, 1.f);
                dirty = true;
            }
        }

        ImGui::Spacing();

        ImGui::SeparatorText(S::Get("HUD_Section_Popup", "Popup").c_str());
        ImGui::Spacing();

        {
            ImGui::SetNextItemWidth(150.f);
            float psr = st.popupSlotRadius;
            if (ImGui::InputFloat(S::Get("HUD_PopupSlotR_Label", "Slot R##popupslotradius").c_str(), &psr, 1.f, 5.f,
                                  "%.0f")) {
                st.popupSlotRadius = std::max(1.f, psr);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float prr = st.popupRingRadius;
            if (ImGui::InputFloat(S::Get("HUD_PopupRingR_Label", "Ring R##popupringradius").c_str(), &prr, 1.f, 5.f,
                                  "%.0f")) {
                st.popupRingRadius = std::max(1.f, prr);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            float psg = st.popupSlotGap;
            if (ImGui::InputFloat(S::Get("HUD_PopupGap_Label", "Gap##popupslotgap").c_str(), &psg, 1.f, 5.f, "%.0f")) {
                st.popupSlotGap = psg;
                dirty = true;
            }
            ImGui::Spacing();

            ImGui::SetNextItemWidth(150.f);
            float mww = st.modeWidgetW;
            if (ImGui::InputFloat(S::Get("HUD_ModeWidgetW_Label", "Widget W##modewidgetw").c_str(), &mww, 1.f, 5.f,
                                  "%.0f")) {
                st.modeWidgetW = std::max(1.f, mww);
                dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            int oa = static_cast<int>(st.overlayAlpha);
            if (ImGui::InputInt(S::Get("HUD_OverlayAlpha_Label", "Overlay Alpha##overlayalpha").c_str(), &oa, 5, 20)) {
                st.overlayAlpha = static_cast<std::uint8_t>(std::clamp(oa, 0, 255));
                dirty = true;
            }
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader(S::Get("HUD_Section_Colors", "Colors").c_str())) {
            ImGui::Spacing();

            auto colorEdit = [&](const char* label, std::uint32_t& col) {
                float c[4];
                c[0] = ((col >> 0) & 0xFF) / 255.f;
                c[1] = ((col >> 8) & 0xFF) / 255.f;
                c[2] = ((col >> 16) & 0xFF) / 255.f;
                c[3] = ((col >> 24) & 0xFF) / 255.f;
                if (ImGui::ColorEdit4(
                        label, c,
                        ImGuiMCP::ImGuiColorEditFlags_AlphaBar | ImGuiMCP::ImGuiColorEditFlags_AlphaPreview)) {
                    col = (static_cast<std::uint32_t>(c[3] * 255.f + .5f) << 24) |
                          (static_cast<std::uint32_t>(c[2] * 255.f + .5f) << 16) |
                          (static_cast<std::uint32_t>(c[1] * 255.f + .5f) << 8) |
                          static_cast<std::uint32_t>(c[0] * 255.f + .5f);
                    dirty = true;
                }
            };
            auto u8Edit = [&](const char* label, std::uint8_t& val) {
                int v = static_cast<int>(val);
                ImGui::SetNextItemWidth(150.f);
                if (ImGui::InputInt(label, &v, 5, 20)) {
                    val = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
                    dirty = true;
                }
            };

            ImGui::SeparatorText(S::Get("HUD_Colors_Slots", "Slots").c_str());
            colorEdit(S::Get("HUD_Color_BgActive", "Bg Active##slotbgactive").c_str(), st.slotBgActive);
            colorEdit(S::Get("HUD_Color_BgInactive", "Bg Inactive##slotbginactive").c_str(), st.slotBgInactive);
            colorEdit(S::Get("HUD_Color_RingInactive", "Ring Inactive##slotringinactive").c_str(), st.slotRingInactive);
            u8Edit(S::Get("HUD_Color_RingActiveAlpha", "Ring Active Alpha##slotringactivealpha").c_str(),
                   st.slotRingActiveAlpha);
            u8Edit(S::Get("HUD_Color_IconAlpha", "Icon Alpha##iconalpha").c_str(), st.iconAlpha);
            colorEdit(S::Get("HUD_Color_EmptySlot", "Empty Slot##emptyslotcolor").c_str(), st.emptySlotColor);

            ImGui::Spacing();
            ImGui::SeparatorText(S::Get("HUD_Colors_RingCenter", "Ring Center").c_str());
            colorEdit(S::Get("HUD_Color_RingCenterFill", "Fill##ringcenterfill").c_str(), st.ringCenterFill);
            colorEdit(S::Get("HUD_Color_RingCenterBorder", "Border##ringcenterborder").c_str(), st.ringCenterBorder);

            ImGui::Spacing();
            ImGui::SeparatorText(S::Get("HUD_Colors_Schools", "School Colors").c_str());

            if (ImGui::BeginTable("##schoolcolors", 2, ImGuiMCP::ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn(S::Get("HUD_Colors_Fill", "Fill").c_str(),
                                        ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(S::Get("HUD_Colors_Glow", "Glow").c_str(),
                                        ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                auto schoolRow = [&](std::string_view nameKey, std::string_view nameFallback, std::uint32_t& fill,
                                     std::uint32_t& glow) {
                    const std::string name = S::Get(nameKey, nameFallback);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    colorEdit((name + "##f").c_str(), fill);
                    ImGui::TableSetColumnIndex(1);
                    colorEdit((name + "##g").c_str(), glow);
                };

                schoolRow("HUD_School_Alteration", "Alteration", st.alterationFill, st.alterationGlow);
                schoolRow("HUD_School_Conjuration", "Conjuration", st.conjurationFill, st.conjurationGlow);
                schoolRow("HUD_School_Destruction", "Destruction", st.destructionFill, st.destructionGlow);
                schoolRow("HUD_School_Illusion", "Illusion", st.illusionFill, st.illusionGlow);
                schoolRow("HUD_School_Restoration", "Restoration", st.restorationFill, st.restorationGlow);
                schoolRow("HUD_School_Default", "Default", st.defaultFill, st.defaultGlow);
                schoolRow("HUD_School_Empty", "Empty", st.emptyFill, st.emptyFill);

                ImGui::EndTable();
            }
            ImGui::Spacing();
        }
    }

    void DrawPatchesTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        if (bool v1 = cfg.skipEquipAnimationPatch;
            ImGui::Checkbox(IntegratedMagic::Strings::Get("Item_SkipEquipAnim", "Skip equip animation").c_str(), &v1)) {
            cfg.skipEquipAnimationPatch = v1;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_SkipEquipAnim",
                                                    "Skips the equip animation when activating a slot via hotkey.\n"
                                                    "Recommended for faster spell swapping.")
                          .c_str());
        }

        if (bool v2 = cfg.skipEquipAnimationOnReturnPatch; ImGui::Checkbox(
                IntegratedMagic::Strings::Get("Item_SkipEquipAnimReturn", "Skip equip animation on return").c_str(),
                &v2)) {
            cfg.skipEquipAnimationOnReturnPatch = v2;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_SkipEquipAnimReturn",
                                                    "Skips the equip animation when restoring the previous snapshot\n"
                                                    "After exiting a slot.")
                          .c_str());
        }

        if (bool v3 = cfg.requireExclusiveHotkeyPatch; ImGui::Checkbox(
                IntegratedMagic::Strings::Get("Item_RequireExclusiveHotkey", "Require exclusive hotkey").c_str(),
                &v3)) {
            cfg.requireExclusiveHotkeyPatch = v3;
            dirty = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_RequireExclusiveHotkey",
                                                    "When enabled, a hotkey combo only triggers if no other keys\n"
                                                    "are held at the same time. Prevents accidental activation\n"
                                                    "during combat or movement.")
                          .c_str());
        }

        if (bool v4 = cfg.pressBothAtSamePatch; ImGui::Checkbox(
                IntegratedMagic::Strings::Get("Item_PressBothAtSame", "Press both at the same time").c_str(), &v4)) {
            cfg.pressBothAtSamePatch = v4;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_PressBothAtSame",
                                                    "When enabled, all keys in a combo must be pressed within\n"
                                                    "0.2 seconds of each other.\n"
                                                    "Holding one key and pressing the other later will not activate\n"
                                                    "the slot.")
                          .c_str());
        }
    }
}

void __stdcall IntegratedMagic::MENU::DrawSettings() {
    auto& cfg = IntegratedMagic::GetMagicConfig();
    bool dirty = false;

    {
        constexpr float kButtonWidth = 160.0f;
        ImGuiMCP::ImVec2 region{};
        ImGui::GetContentRegionAvail(&region);
        const float rightEdge = ImGui::GetCursorPosX() + region.x;
        ImGui::SetCursorPosX(rightEdge - kButtonWidth);
        ImGui::BeginDisabled(!g_pending);
        if (ImGui::Button(IntegratedMagic::Strings::Get("Item_Apply", "Apply changes").c_str(),
                          ImGui::ImVec2{kButtonWidth, 0.0f})) {
            cfg.Save();
            IntegratedMagic::StyleConfig::Get().Save();
            if (IntegratedMagic::SpellSettingsDB::Get().IsDirty()) {
                IntegratedMagic::SpellSettingsDB::Get().Save();
                IntegratedMagic::SpellSettingsDB::Get().ClearDirty();
            }
            Input::OnConfigChanged();
            g_pending = false;
        }
        ImGui::EndDisabled();
    }
    ImGui::Spacing();

    if (ImGui::BeginTabBar("IMAGIC_TABS")) {
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_General", "General").c_str())) {
            DrawGeneralTab(cfg, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Controls", "Controls").c_str())) {
            DrawControlsTab(cfg, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_HUD", "HUD").c_str())) {
            DrawHudTab(dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Patches", "Patches").c_str())) {
            DrawPatchesTab(cfg, dirty);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (dirty) {
        g_pending = true;
    }
}

void IntegratedMagic::MENU::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }
    SKSEMenuFramework::SetSection(IntegratedMagic::Strings::Get("SectionName", "Integrated Magic"));
    SKSEMenuFramework::AddSectionItem(IntegratedMagic::Strings::Get("SectionItem_Settings", "Settings"), DrawSettings);
}