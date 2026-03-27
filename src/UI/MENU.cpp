#include "MENU.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>

#include "Config/Config.h"
#include "Config/SpellType.h"
#include "Input/Input.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"
#include "SKSEMenuFramework.h"
#include "Strings.h"
#include "UI/HudManager.h"
#include "UI/StyleConfig.h"

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
        ImGuiMCP::SetNextItemWidth(width);
        if (ImGuiMCP::InputInt(label.c_str(), &v)) {
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
            Input::SetCaptureModeActive(false);
            g_fieldCapture = {};
        }
    }

    void DrawKeyValueCapture(std::atomic<int>& field, bool wantKeyboard, bool& dirty, int rowPos = 0,
                             IntegratedMagic::MagicConfig* cfg = nullptr) {
        ImGuiMCP::PushID(static_cast<void*>(&field));

        ImGuiMCP::SetNextItemWidth(70.0f);
        if (int v = field.load(std::memory_order_relaxed); ImGuiMCP::InputInt("##v", &v, 0, 0)) {
            v = ClampMinusOne(v);
            field.store(v, std::memory_order_relaxed);
            dirty = true;
        }
        ImGuiMCP::SameLine();

        if (const bool isThis = g_fieldCapture.active && g_fieldCapture.field == &field; isThis) {
            if (const int encoded = Input::PollCapturedHotkey(); encoded != -1) {
                const bool gotKb = (encoded >= 0);
                if (gotKb == wantKeyboard) {
                    const int val = wantKeyboard ? encoded : -(encoded + 2);
                    field.store(val, std::memory_order_relaxed);
                    dirty = true;
                    g_fieldCapture = {};
                    Input::SetCaptureModeActive(false);
                } else {
                    Input::RequestHotkeyCapture();
                    Input::SetCaptureModeActive(true);
                }
            }

            ImGuiMCP::TextDisabled("...");
            ImGuiMCP::SameLine();
            if (ImGuiMCP::SmallButton("X")) {
                CancelFieldCapture();
            }
        } else {
            if (g_fieldCapture.active) ImGuiMCP::BeginDisabled(true);
            if (ImGuiMCP::SmallButton(IntegratedMagic::Strings::Get("Btn_Cap", "Cap").c_str())) {
                g_fieldCapture = {&field, wantKeyboard, true};
                Input::RequestHotkeyCapture();
                Input::SetCaptureModeActive(true);
            }
            if (g_fieldCapture.active) ImGuiMCP::EndDisabled();
        }

        if (cfg && rowPos > 0) {
            ImGuiMCP::SameLine();
            int& modPos = wantKeyboard ? cfg->modifierKeyboardPosition : cfg->modifierGamepadPosition;
            const bool isMod = (modPos == rowPos);
            if (isMod) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
            ImGuiMCP::PushID(wantKeyboard ? "kbm" : "gpm");
            if (ImGuiMCP::SmallButton("M")) {
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
            if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip(isMod ? "Unmark as modifier" : "Mark as modifier");
            ImGuiMCP::PopID();
            if (isMod) ImGuiMCP::PopStyleColor();
        }

        ImGuiMCP::PopID();
    }

    void DrawInputDetailRow(const char* kbLabel, std::atomic<int>& kbField, const char* gpLabel,
                            std::atomic<int>& gpField, bool& dirty, int rowPos, IntegratedMagic::MagicConfig& cfg) {
        ImGuiMCP::TableNextRow();

        ImGuiMCP::TableSetColumnIndex(0);
        ImGuiMCP::TextUnformatted(kbLabel);

        ImGuiMCP::TableSetColumnIndex(1);
        {
            const bool dirtyBefore = dirty;
            DrawKeyValueCapture(kbField, true, dirty, rowPos, &cfg);
            if (dirty && !dirtyBefore && rowPos > 0 && cfg.modifierKeyboardPosition == rowPos) {
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

        ImGuiMCP::TableSetColumnIndex(2);
        ImGuiMCP::TextUnformatted(gpLabel);

        ImGuiMCP::TableSetColumnIndex(3);
        {
            const bool dirtyBefore = dirty;
            DrawKeyValueCapture(gpField, false, dirty, rowPos, &cfg);
            if (dirty && !dirtyBefore && rowPos > 0 && cfg.modifierGamepadPosition == rowPos) {
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
        ImGuiMCP::TextUnformatted(title);
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        if (const ImGuiMCP::ImGuiTableFlags kTableFlags =
                ImGuiMCP::ImGuiTableFlags_BordersOuter | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_SizingFixedFit;
            ImGuiMCP::BeginTable("##InputDetail", 4, kTableFlags)) {
            ImGuiMCP::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Keyboard", "Keyboard").c_str(),
                                       ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGuiMCP::TableSetupColumn("##kbv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGuiMCP::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Gamepad", "Gamepad").c_str(),
                                       ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGuiMCP::TableSetupColumn("##gpv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGuiMCP::TableHeadersRow();

            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key1", "Key 1").c_str(), icfg.KeyboardScanCode1,
                               IntegratedMagic::Strings::Get("Item_Btn1", "Btn 1").c_str(), icfg.GamepadButton1, dirty,
                               showModifier ? 1 : 0, cfg);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key2", "Key 2").c_str(), icfg.KeyboardScanCode2,
                               IntegratedMagic::Strings::Get("Item_Btn2", "Btn 2").c_str(), icfg.GamepadButton2, dirty,
                               showModifier ? 2 : 0, cfg);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key3", "Key 3").c_str(), icfg.KeyboardScanCode3,
                               IntegratedMagic::Strings::Get("Item_Btn3", "Btn 3").c_str(), icfg.GamepadButton3, dirty,
                               showModifier ? 3 : 0, cfg);

            ImGuiMCP::EndTable();
        }

        if (g_fieldCapture.active) {
            ImGuiMCP::Spacing();
            const auto msg = std::format(
                "{}...", g_fieldCapture.wantKeyboard
                             ? IntegratedMagic::Strings::Get("Capture_WaitKb", "Press a keyboard key or mouse button")
                             : IntegratedMagic::Strings::Get("Capture_WaitGp", "Press a gamepad button"));
            ImGuiMCP::TextDisabled("%s", msg.c_str());
        }
    }

    void DrawGeneralTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        ImGuiMCP::Spacing();

        const auto oldCount = static_cast<int>(cfg.SlotCount());
        int n = oldCount;
        ImGuiMCP::SetNextItemWidth(180.0f);
        if (ImGuiMCP::InputInt(IntegratedMagic::Strings::Get("Item_SlotCount", "Slot count").c_str(), &n)) {
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

        ImGuiMCP::Spacing();
        ImGuiMCP::SeparatorText(IntegratedMagic::Strings::Get("HUD_Visibility_Label", "HUD Visibility").c_str());

        using F = IntegratedMagic::HudVisibilityFlag;
        auto flagCheck = [&](F flag, const char* strKey, const char* fallback) {
            bool v = cfg.HudFlagSet(flag);
            if (ImGuiMCP::Checkbox(IntegratedMagic::Strings::Get(strKey, fallback).c_str(), &v)) {
                if (v)
                    cfg.hudVisibilityFlags |= static_cast<std::uint8_t>(flag);
                else
                    cfg.hudVisibilityFlags &= ~static_cast<std::uint8_t>(flag);
                dirty = true;
            }
        };

        flagCheck(F::Always, "HUD_Show_Always", "Always");
        flagCheck(F::SlotActive, "HUD_Show_SlotActive", "Slot Active");
        flagCheck(F::InCombat, "HUD_Show_InCombat", "In Combat");
        flagCheck(F::WeaponDrawn, "HUD_Show_WeaponDrawn", "Weapon Drawn");

        if (cfg.hudVisibilityFlags == 0) {
            ImGuiMCP::SameLine();
            ImGuiMCP::TextDisabled("(%s)", IntegratedMagic::Strings::Get("HUD_Show_Never_Hint", "HUD hidden").c_str());
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        {
            auto& st = IntegratedMagic::StyleConfig::Get();
            const std::string iconTypeNames =
                IntegratedMagic::Strings::Get("Item_ButtonIcon_Keyboard", "Keyboard") + '\0' +
                IntegratedMagic::Strings::Get("Item_ButtonIcon_PlayStation", "PlayStation") + '\0' +
                IntegratedMagic::Strings::Get("Item_ButtonIcon_Xbox", "Xbox") + '\0';
            int iconTypeIdx = static_cast<int>(st.buttonIconType);
            ImGuiMCP::SetNextItemWidth(180.0f);
            if (ImGuiMCP::Combo(
                    IntegratedMagic::Strings::Get("Item_ButtonIconType", "Button icons##buttonicons").c_str(),
                    &iconTypeIdx, iconTypeNames.c_str())) {
                st.buttonIconType = static_cast<IntegratedMagic::ButtonIconType>(iconTypeIdx);
                dirty = true;
            }
        }

        ImGuiMCP::SeparatorText(
            IntegratedMagic::Strings::Get("Section_SpellTypeDefaults", "Default spell behavior").c_str());

        struct TypeEntry {
            IntegratedMagic::SpellType type;
            const char* labelKey;
            const char* labelFallback;
        };
        constexpr TypeEntry kTypeEntries[] = {
            {IntegratedMagic::SpellType::Concentration, "SpellType_Concentration", "Concentration"},
            {IntegratedMagic::SpellType::Cast, "SpellType_Cast", "Cast"},
            {IntegratedMagic::SpellType::Bound, "SpellType_Bound", "Bound weapon"},
            {IntegratedMagic::SpellType::Power, "SpellType_Power", "Power"},
            {IntegratedMagic::SpellType::Shout, "SpellType_Shout", "Shout"},
        };

        const std::string modeComboItems = IntegratedMagic::Strings::Get("Mode_Hold", "Hold") + '\0' +
                                           IntegratedMagic::Strings::Get("Mode_Press", "Press") + '\0' +
                                           IntegratedMagic::Strings::Get("Mode_Automatic", "Automatic") + '\0';

        for (const auto& e : kTypeEntries) {
            auto& d = cfg.spellTypeDefaults[static_cast<int>(e.type)];
            const std::string label = IntegratedMagic::Strings::Get(e.labelKey, e.labelFallback);

            ImGuiMCP::PushID(e.labelKey);

            ImGuiMCP::Text("%s", label.c_str());
            ImGuiMCP::SameLine(180.f);

            int modeIdx = static_cast<int>(d.mode);
            ImGuiMCP::SetNextItemWidth(120.f);
            if (ImGuiMCP::Combo(IntegratedMagic::Strings::Get("Item_Mode", "Mode##mode").c_str(), &modeIdx,
                                modeComboItems.c_str())) {
                d.mode = static_cast<IntegratedMagic::ActivationMode>(modeIdx);
                dirty = true;
            }

            if (d.mode == IntegratedMagic::ActivationMode::Hold || d.mode == IntegratedMagic::ActivationMode::Press) {
                ImGuiMCP::SameLine();
                bool aa = d.autoAttack;
                if (ImGuiMCP::Checkbox(IntegratedMagic::Strings::Get("Item_AutoCast", "Auto-cast##autocast").c_str(),
                                       &aa)) {
                    d.autoAttack = aa;
                    dirty = true;
                }
            }

            ImGuiMCP::PopID();
        }
    }

    void DrawControlsTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        const auto n = static_cast<int>(cfg.SlotCount());

        if (g_selectedSlot >= n) {
            CancelFieldCapture();
            g_selectedSlot = n > 0 ? 0 : kHudPopupSlot;
        }

        ImGuiMCP::BeginChild("##CtrlList", ImGuiMCP::ImVec2{145.0f, 0.0f}, true);

        for (int slot = 0; slot < n; ++slot) {
            const auto& icfg = cfg.slotInput[static_cast<std::size_t>(slot)];
            const bool hasKey = SlotHasHotkey(icfg);

            const auto label = std::format(
                "{}  {}",
                IntegratedMagic::Strings::Get(std::format("List_Magic{}", slot + 1), std::format("Magic {}", slot + 1)),
                hasKey ? "*" : "-");

            if (ImGuiMCP::Selectable(label.c_str(), g_selectedSlot == slot) && (g_selectedSlot != slot)) {
                CancelFieldCapture();
                g_selectedSlot = slot;
            }
        }

        ImGuiMCP::Separator();

        {
            const bool hasKey = SlotHasHotkey(cfg.hudPopupInput);
            const auto label =
                std::format("{}  {}", IntegratedMagic::Strings::Get("List_HudPopup", "HUD Popup"), hasKey ? "*" : "-");
            if (ImGuiMCP::Selectable(label.c_str(), g_selectedSlot == kHudPopupSlot) &&
                (g_selectedSlot != kHudPopupSlot)) {
                CancelFieldCapture();
                g_selectedSlot = kHudPopupSlot;
            }
        }

        ImGuiMCP::EndChild();
        ImGuiMCP::SameLine();

        ImGuiMCP::BeginChild("##CtrlDetail", ImGuiMCP::ImVec2{0.0f, 0.0f}, true);

        if (g_selectedSlot == kHudPopupSlot) {
            DrawDetailPanel(cfg.hudPopupInput, IntegratedMagic::Strings::Get("Detail_HudPopup", "HUD Popup").c_str(),
                            dirty, cfg, false);
        } else if (g_selectedSlot >= 0 && g_selectedSlot < n) {
            const auto title = IntegratedMagic::Strings::Get(std::format("Detail_Magic{}", g_selectedSlot + 1),
                                                             std::format("Magic {}", g_selectedSlot + 1));
            DrawDetailPanel(cfg.slotInput[static_cast<std::size_t>(g_selectedSlot)], title.c_str(), dirty, cfg);
        }

        ImGuiMCP::EndChild();
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
                if (col > 0) ImGuiMCP::SameLine(0.f, 2.f);
                const auto& cell = kGrid[row][col];
                const bool active = (st.hudAnchor == cell.anchor);
                if (active) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
                if (ImGuiMCP::Button(cell.label, {28.f, 20.f})) {
                    st.hudAnchor = cell.anchor;
                    dirty = true;
                }
                if (active) ImGuiMCP::PopStyleColor();
            }
        }
    }

    void DrawShapeEditor(bool& dirty) {
        namespace S = IntegratedMagic::Strings;
        auto& shape = IntegratedMagic::StyleConfig::Get().slotShape;
        auto& verts = shape.vertices;

        constexpr float kCanvasSize = 200.f;
        constexpr float kRadius = 80.f;

        ImGuiMCP::ImVec2 origin{};
        ImGuiMCP::GetCursorScreenPos(&origin);
        const ImGuiMCP::ImVec2 center = {origin.x + kCanvasSize * 0.5f, origin.y + kCanvasSize * 0.5f};
        const ImGuiMCP::ImVec2 canvasEnd = {origin.x + kCanvasSize, origin.y + kCanvasSize};

        ImGuiMCP::InvisibleButton("##shapeeditorcanvas", {kCanvasSize, kCanvasSize});
        const bool canvasHovered = ImGuiMCP::IsItemHovered();

        ImGuiMCP::ImDrawList* dl = ImGuiMCP::GetWindowDrawList();
        namespace DL = ImGuiMCP::ImDrawListManager;

        DL::AddRectFilled(dl, origin, canvasEnd, IM_COL32(25, 25, 25, 220), 4.f, 0);
        DL::AddRect(dl, origin, canvasEnd, IM_COL32(80, 80, 80, 180), 4.f, 0, 1.f);
        DL::AddCircle(dl, center, kRadius, IM_COL32(55, 55, 55, 255), 48, 1.f);
        DL::AddLine(dl, {center.x - kRadius, center.y}, {center.x + kRadius, center.y}, IM_COL32(50, 50, 50, 200), 1.f);
        DL::AddLine(dl, {center.x, center.y - kRadius}, {center.x, center.y + kRadius}, IM_COL32(50, 50, 50, 200), 1.f);

        if (verts.size() >= 3) {
            for (const auto& v : verts) DL::PathLineTo(dl, {center.x + v.x * kRadius, center.y + v.y * kRadius});
            DL::PathFillConvex(dl, IM_COL32(100, 160, 255, 50));

            for (const auto& v : verts) DL::PathLineTo(dl, {center.x + v.x * kRadius, center.y + v.y * kRadius});
            DL::PathStroke(dl, IM_COL32(100, 160, 255, 200), ImGuiMCP::ImDrawFlags_Closed, 1.5f);
        }

        const ImGuiMCP::ImVec2 mousePos = ImGuiMCP::GetIO()->MousePos;
        static int s_dragging = -1;

        for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
            const ImGuiMCP::ImVec2 vp = {center.x + verts[i].x * kRadius, center.y + verts[i].y * kRadius};
            const float dx = mousePos.x - vp.x;
            const float dy = mousePos.y - vp.y;
            const bool hovered = canvasHovered && (dx * dx + dy * dy) < 64.f;

            if (hovered && ImGuiMCP::IsMouseClicked(0)) s_dragging = i;

            if (s_dragging == i && ImGuiMCP::IsMouseDown(0)) {
                verts[i].x = std::clamp((mousePos.x - center.x) / kRadius, -1.f, 1.f);
                verts[i].y = std::clamp((mousePos.y - center.y) / kRadius, -1.f, 1.f);
                dirty = true;
            }

            const ImGuiMCP::ImU32 col =
                (hovered || s_dragging == i) ? IM_COL32(255, 200, 80, 255) : IM_COL32(200, 200, 200, 220);
            DL::AddCircleFilled(dl, vp, 6.f, col, 12);
            DL::AddCircle(dl, vp, 6.f, IM_COL32(0, 0, 0, 180), 12, 1.f);

            if (hovered) ImGuiMCP::SetTooltip("%.2f, %.2f", verts[i].x, verts[i].y);
        }

        if (!ImGuiMCP::IsMouseDown(0)) s_dragging = -1;

        ImGuiMCP::Spacing();

        if (ImGuiMCP::Button(S::Get("Shape_AddVertex", "+ Vertex").c_str())) {
            if (verts.size() >= 2) {
                const auto& a = verts[verts.size() - 1];
                const auto& b = verts[0];
                verts.push_back({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f});
            } else {
                verts.push_back({0.f, -1.f});
            }
            dirty = true;
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::BeginDisabled(verts.size() <= 3);
        if (ImGuiMCP::Button(S::Get("Shape_RemoveVertex", "- Vertex").c_str())) {
            verts.pop_back();
            dirty = true;
        }
        ImGuiMCP::EndDisabled();

        ImGuiMCP::Spacing();
        ImGuiMCP::SeparatorText(S::Get("Shape_Presets", "Presets").c_str());
        ImGuiMCP::Spacing();

        if (ImGuiMCP::Button(S::Get("Shape_Circle", "Circle").c_str())) {
            shape.SetCircle(16);
            dirty = true;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(S::Get("Shape_Square", "Square").c_str())) {
            shape.SetSquare();
            dirty = true;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(S::Get("Shape_Diamond", "Diamond").c_str())) {
            shape.SetDiamond();
            dirty = true;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(S::Get("Shape_Star5", "Star (5)").c_str())) {
            shape.SetStar(5, 0.45f);
            dirty = true;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(S::Get("Shape_Star6", "Star (6)").c_str())) {
            shape.SetStar(6, 0.45f);
            dirty = true;
        }
    }

    void DrawHudTab(bool& dirty) {
        namespace S = IntegratedMagic::Strings;
        auto& st = IntegratedMagic::StyleConfig::Get();

        ImGuiMCP::SeparatorText(S::Get("HUD_Section_Layout", "Layout").c_str());
        ImGuiMCP::Spacing();

        {
            using LT = IntegratedMagic::HudLayoutType;
            const std::string layoutNames =
                S::Get("HUD_Layout_Circular", "Circular") + '\0' + S::Get("HUD_Layout_Horizontal", "Horizontal") +
                '\0' + S::Get("HUD_Layout_Vertical", "Vertical") + '\0' + S::Get("HUD_Layout_Grid", "Grid") + '\0';
            int layoutIdx = static_cast<int>(st.hudLayout);
            ImGuiMCP::SetNextItemWidth(150.f);
            if (ImGuiMCP::Combo(S::Get("HUD_Layout_Label", "Layout##hudlayout").c_str(), &layoutIdx,
                                layoutNames.c_str())) {
                st.hudLayout = static_cast<LT>(layoutIdx);
                dirty = true;
            }

            {
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(150.f);
                float spacing = st.slotSpacing;
                if (ImGuiMCP::InputFloat(S::Get("HUD_Spacing_Label", "Spacing##slotspacing").c_str(), &spacing, 1.f,
                                         5.f, "%.1f")) {
                    st.slotSpacing = spacing;
                    dirty = true;
                }
            }
            if (st.hudLayout == LT::Grid) {
                ImGuiMCP::SetNextItemWidth(150.f);
                int cols = st.gridColumns;
                if (ImGuiMCP::InputInt(S::Get("HUD_Columns_Label", "Columns##gridcols").c_str(), &cols, 1, 1)) {
                    st.gridColumns = std::max(1, cols);
                    dirty = true;
                }
            }
            if (st.hudLayout == LT::Circular) {
                ImGuiMCP::SetNextItemWidth(150.f);
                float rr = st.ringRadius;
                if (ImGuiMCP::InputFloat(S::Get("HUD_RingRadius_Label", "Ring R##ringradius").c_str(), &rr, 1.f, 5.f,
                                         "%.1f")) {
                    st.ringRadius = rr;
                    dirty = true;
                }
            }
        }

        ImGuiMCP::Spacing();

        ImGuiMCP::SeparatorText(S::Get("HUD_Section_Position", "Position").c_str());
        ImGuiMCP::Spacing();

        DrawAnchorWidget(dirty);

        ImGuiMCP::SameLine(0.f, 16.f);
        ImGuiMCP::BeginGroup();
        ImGuiMCP::SetNextItemWidth(150.f);
        float ox = st.hudOffsetX;
        if (ImGuiMCP::InputFloat(S::Get("HUD_OffsetX_Label", "X##hudox").c_str(), &ox, 1.f, 5.f, "%.0f")) {
            st.hudOffsetX = ox;
            dirty = true;
        }
        ImGuiMCP::SetNextItemWidth(150.f);
        float oy = st.hudOffsetY;
        if (ImGuiMCP::InputFloat(S::Get("HUD_OffsetY_Label", "Y##hudoy").c_str(), &oy, 1.f, 5.f, "%.0f")) {
            st.hudOffsetY = oy;
            dirty = true;
        }
        ImGuiMCP::EndGroup();

        ImGuiMCP::Spacing();

        ImGuiMCP::SeparatorText(S::Get("HUD_Section_Slots", "Slots").c_str());
        ImGuiMCP::Spacing();

        {
            ImGuiMCP::SetNextItemWidth(150.f);
            float sr = st.slotRadius;
            if (ImGuiMCP::InputFloat(S::Get("HUD_SlotRadius_Label", "Radius##slotradius").c_str(), &sr, 1.f, 5.f,
                                     "%.1f")) {
                st.slotRadius = sr;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float rw = st.slotRingWidth;
            if (ImGuiMCP::InputFloat(S::Get("HUD_RingWidth_Label", "Ring Width##ringwidth").c_str(), &rw, 0.5f, 1.f,
                                     "%.1f")) {
                st.slotRingWidth = std::max(0.f, rw);
                dirty = true;
            }
            ImGuiMCP::Spacing();

            ImGuiMCP::SetNextItemWidth(150.f);
            float as = st.slotActiveScale;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ActiveScale_Label", "Active##activescale").c_str(), &as, 0.05f, 0.1f,
                                     "%.2f")) {
                st.slotActiveScale = as;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float ms = st.slotModifierScale;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ModifierScale_Label", "Modifier##modscale").c_str(), &ms, 0.05f, 0.1f,
                                     "%.2f")) {
                st.slotModifierScale = ms;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float ns = st.slotNeighborScale;
            if (ImGuiMCP::InputFloat(S::Get("HUD_NeighborScale_Label", "Neighbor##neighborscale").c_str(), &ns, 0.05f,
                                     0.1f, "%.2f")) {
                st.slotNeighborScale = ns;
                dirty = true;
            }
            ImGuiMCP::Spacing();

            ImGuiMCP::SetNextItemWidth(150.f);
            float et = st.slotExpandTime;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ExpandTime_Label", "Expand##expandtime").c_str(), &et, 0.01f, 0.05f,
                                     "%.2f")) {
                st.slotExpandTime = std::max(0.f, et);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float rt = st.slotRetractTime;
            if (ImGuiMCP::InputFloat(S::Get("HUD_RetractTime_Label", "Retract##retracttime").c_str(), &rt, 0.01f, 0.05f,
                                     "%.2f")) {
                st.slotRetractTime = std::max(0.f, rt);
                dirty = true;
            }
        }

        ImGuiMCP::Spacing();

        ImGuiMCP::SeparatorText(S::Get("HUD_Section_Icons", "Icons").c_str());
        ImGuiMCP::Spacing();

        {
            ImGuiMCP::SetNextItemWidth(150.f);
            float isf = st.iconSizeFactor;
            if (ImGuiMCP::InputFloat(S::Get("HUD_IconSize_Label", "Size##iconsizefactor").c_str(), &isf, 0.05f, 0.1f,
                                     "%.2f")) {
                st.iconSizeFactor = std::clamp(isf, 0.1f, 2.f);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float iof = st.iconOffsetFactor;
            if (ImGuiMCP::InputFloat(S::Get("HUD_IconOffset_Label", "Offset##iconoffsetfactor").c_str(), &iof, 0.05f,
                                     0.1f, "%.2f")) {
                st.iconOffsetFactor = std::clamp(iof, 0.f, 1.f);
                dirty = true;
            }
        }

        ImGuiMCP::Spacing();

        ImGuiMCP::SeparatorText(S::Get("HUD_Section_Popup", "Popup").c_str());
        ImGuiMCP::Spacing();

        {
            const std::string popupLayoutNames =
                S::Get("HUD_Layout_Circular", "Circular") + '\0' + S::Get("HUD_Layout_Horizontal", "Horizontal") +
                '\0' + S::Get("HUD_Layout_Vertical", "Vertical") + '\0' + S::Get("HUD_Layout_Grid", "Grid") + '\0';
            int popupLayoutIdx = static_cast<int>(st.popupLayout);
            ImGuiMCP::SetNextItemWidth(180.f);
            if (ImGuiMCP::Combo(S::Get("HUD_PopupLayout_Label", "Layout##popuplayout").c_str(), &popupLayoutIdx,
                                popupLayoutNames.c_str())) {
                st.popupLayout = static_cast<IntegratedMagic::HudLayoutType>(popupLayoutIdx);
                dirty = true;
            }
            ImGuiMCP::Spacing();
        }

        {
            ImGuiMCP::SetNextItemWidth(150.f);
            float psr = st.popupSlotRadius;
            if (ImGuiMCP::InputFloat(S::Get("HUD_PopupSlotR_Label", "Slot R##popupslotradius").c_str(), &psr, 1.f, 5.f,
                                     "%.0f")) {
                st.popupSlotRadius = std::max(1.f, psr);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float prr = st.popupRingRadius;
            if (ImGuiMCP::InputFloat(S::Get("HUD_PopupRingR_Label", "Ring R##popupringradius").c_str(), &prr, 1.f, 5.f,
                                     "%.0f")) {
                st.popupRingRadius = std::max(1.f, prr);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float psg = st.popupSlotGap;
            if (ImGuiMCP::InputFloat(S::Get("HUD_PopupGap_Label", "Gap##popupslotgap").c_str(), &psg, 1.f, 5.f,
                                     "%.0f")) {
                st.popupSlotGap = psg;
                dirty = true;
            }
            ImGuiMCP::Spacing();

            ImGuiMCP::SetNextItemWidth(150.f);
            float mww = st.modeWidgetW;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ModeWidgetW_Label", "Widget W##modewidgetw").c_str(), &mww, 1.f, 5.f,
                                     "%.0f")) {
                st.modeWidgetW = std::max(1.f, mww);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            int oa = static_cast<int>(st.overlayAlpha);
            if (ImGuiMCP::InputInt(S::Get("HUD_OverlayAlpha_Label", "Overlay Alpha##overlayalpha").c_str(), &oa, 5,
                                   20)) {
                st.overlayAlpha = static_cast<std::uint8_t>(std::clamp(oa, 0, 255));
                dirty = true;
            }
        }

        ImGuiMCP::Spacing();

        if (ImGuiMCP::CollapsingHeader(S::Get("HUD_Section_SlotShape", "Slot Shape").c_str())) {
            ImGuiMCP::Spacing();
            DrawShapeEditor(dirty);
            ImGuiMCP::Spacing();
        }

        ImGuiMCP::Spacing();

        if (ImGuiMCP::CollapsingHeader(S::Get("HUD_Section_Modifier", "Modifier Button").c_str())) {
            ImGuiMCP::Spacing();

            const std::string modVisNames = S::Get("Item_ModVis_Never", "Never") + '\0' +
                                            S::Get("Item_ModVis_Always", "Always") + '\0' +
                                            S::Get("Item_ModVis_HideOnPress", "Hide on press") + '\0';
            int modVisIdx = static_cast<int>(st.modifierWidgetVisibility);
            ImGuiMCP::SetNextItemWidth(180.f);
            if (ImGuiMCP::Combo(S::Get("Item_ModifierVisibility", "Visibility##modvis").c_str(), &modVisIdx,
                                modVisNames.c_str())) {
                st.modifierWidgetVisibility = static_cast<IntegratedMagic::ModifierWidgetVisibility>(modVisIdx);
                dirty = true;
            }
            ImGuiMCP::Spacing();

            ImGuiMCP::SetNextItemWidth(150.f);
            float mr = st.modifierWidgetRadius;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ModWidget_Radius", "Radius##modwidgetradius").c_str(), &mr, 1.f, 5.f,
                                     "%.1f")) {
                st.modifierWidgetRadius = std::max(1.f, mr);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float mox = st.modifierWidgetOffsetX;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ModWidget_OffsetX", "X##modwidgetox").c_str(), &mox, 1.f, 5.f,
                                     "%.0f")) {
                st.modifierWidgetOffsetX = mox;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float moy = st.modifierWidgetOffsetY;
            if (ImGuiMCP::InputFloat(S::Get("HUD_ModWidget_OffsetY", "Y##modwidgetoy").c_str(), &moy, 1.f, 5.f,
                                     "%.0f")) {
                st.modifierWidgetOffsetY = moy;
                dirty = true;
            }
        }

        ImGuiMCP::Spacing();

        if (ImGuiMCP::CollapsingHeader(S::Get("HUD_Section_ButtonLabels", "Button Labels").c_str())) {
            ImGuiMCP::Spacing();

            const std::string lblVisNames = S::Get("HUD_BtnLbl_Never", "Never") + '\0' +
                                            S::Get("HUD_BtnLbl_Always", "Always") + '\0' +
                                            S::Get("HUD_BtnLbl_OnModifier", "On modifier") + '\0';
            int lblVisIdx = static_cast<int>(st.buttonLabelVisibility);
            ImGuiMCP::SetNextItemWidth(180.f);
            if (ImGuiMCP::Combo(S::Get("HUD_BtnLbl_Visibility", "Visibility##btnlblvis").c_str(), &lblVisIdx,
                                lblVisNames.c_str())) {
                st.buttonLabelVisibility = static_cast<IntegratedMagic::ButtonLabelVisibility>(lblVisIdx);
                dirty = true;
            }

            ImGuiMCP::Spacing();

            {
                using C = IntegratedMagic::ButtonLabelCorner;
                ImGuiMCP::TextDisabled("%s", S::Get("HUD_BtnLbl_Corner", "Corner").c_str());

                constexpr float kBtnSz = 28.f;
                constexpr float kGap = 2.f;
                constexpr float kInvis = kBtnSz + kGap;

                auto cornerBtn = [&](C corner, const char* id) {
                    const bool active = (st.buttonLabelCorner == corner);
                    if (active) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
                    if (ImGuiMCP::Button(id, {kBtnSz, kBtnSz})) {
                        st.buttonLabelCorner = corner;
                        dirty = true;
                    }
                    if (active) ImGuiMCP::PopStyleColor();
                };

                ImGuiMCP::Dummy({kInvis, kBtnSz});
                ImGuiMCP::SameLine(0.f, kGap);
                cornerBtn(C::Top, "##BL_T");
                ImGuiMCP::SameLine(0.f, kGap);
                ImGuiMCP::Dummy({kInvis, kBtnSz});

                cornerBtn(C::Left, "##BL_L");
                ImGuiMCP::SameLine(0.f, kGap);
                ImGuiMCP::Dummy({kBtnSz, kBtnSz});
                ImGuiMCP::SameLine(0.f, kGap);
                cornerBtn(C::Right, "##BL_R");

                ImGuiMCP::Dummy({kInvis, kBtnSz});
                ImGuiMCP::SameLine(0.f, kGap);
                cornerBtn(C::Bottom, "##BL_B");

                ImGuiMCP::SameLine(0.f, 20.f);
                ImGuiMCP::BeginGroup();
                {
                    const bool tc = (st.buttonLabelCorner == C::TowardCenter);
                    if (tc) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
                    if (ImGuiMCP::Button(S::Get("HUD_BtnLbl_TowardCenter", "Inward##tc").c_str(),
                                         {0.f, kBtnSz * 1.5f})) {
                        st.buttonLabelCorner = C::TowardCenter;
                        dirty = true;
                    }
                    if (tc) ImGuiMCP::PopStyleColor();
                }
                {
                    const bool ac = (st.buttonLabelCorner == C::AwayFromCenter);
                    if (ac) ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, IM_COL32(180, 120, 30, 220));
                    if (ImGuiMCP::Button(S::Get("HUD_BtnLbl_AwayFromCenter", "Outward##ac").c_str(),
                                         {0.f, kBtnSz * 1.5f})) {
                        st.buttonLabelCorner = C::AwayFromCenter;
                        dirty = true;
                    }
                    if (ac) ImGuiMCP::PopStyleColor();
                }
                ImGuiMCP::EndGroup();
            }

            ImGuiMCP::Spacing();

            ImGuiMCP::SetNextItemWidth(150.f);
            float iconSz = st.buttonLabelIconSize;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_IconSize", "Icon size##btnlblsz").c_str(), &iconSz, 1.f, 5.f,
                                     "%.0f")) {
                st.buttonLabelIconSize = std::max(4.f, iconSz);
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float iconSpc = st.buttonLabelIconSpacing;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_IconSpacing", "Spacing##btnlblspc").c_str(), &iconSpc, 1.f, 5.f,
                                     "%.0f")) {
                st.buttonLabelIconSpacing = iconSpc;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float margin = st.buttonLabelMargin;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_Margin", "Margin##btnlblmar").c_str(), &margin, 1.f, 5.f,
                                     "%.0f")) {
                st.buttonLabelMargin = margin;
                dirty = true;
            }

            ImGuiMCP::SetNextItemWidth(150.f);
            float ox2 = st.buttonLabelOffsetX;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_OffsetX", "X##btnlblox").c_str(), &ox2, 1.f, 5.f, "%.0f")) {
                st.buttonLabelOffsetX = ox2;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float oy2 = st.buttonLabelOffsetY;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_OffsetY", "Y##btnlbloy").c_str(), &oy2, 1.f, 5.f, "%.0f")) {
                st.buttonLabelOffsetY = oy2;
                dirty = true;
            }
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(150.f);
            float ft = st.buttonLabelFadeTime;
            if (ImGuiMCP::InputFloat(S::Get("HUD_BtnLbl_FadeTime", "Fade##btnlblft").c_str(), &ft, 0.01f, 0.05f,
                                     "%.2f")) {
                st.buttonLabelFadeTime = std::max(0.f, ft);
                dirty = true;
            }
        }

        ImGuiMCP::Spacing();

        if (ImGuiMCP::CollapsingHeader(S::Get("HUD_Section_Colors", "Colors").c_str())) {
            ImGuiMCP::Spacing();

            auto colorEdit = [&](const char* label, std::uint32_t& col) {
                float c[4];
                c[0] = ((col >> 0) & 0xFF) / 255.f;
                c[1] = ((col >> 8) & 0xFF) / 255.f;
                c[2] = ((col >> 16) & 0xFF) / 255.f;
                c[3] = ((col >> 24) & 0xFF) / 255.f;
                if (ImGuiMCP::ColorEdit4(
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
                ImGuiMCP::SetNextItemWidth(150.f);
                if (ImGuiMCP::InputInt(label, &v, 5, 20)) {
                    val = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
                    dirty = true;
                }
            };

            ImGuiMCP::SeparatorText(S::Get("HUD_Colors_Slots", "Slots").c_str());
            colorEdit(S::Get("HUD_Color_BgActive", "Bg Active##slotbgactive").c_str(), st.slotBgActive);
            colorEdit(S::Get("HUD_Color_BgInactive", "Bg Inactive##slotbginactive").c_str(), st.slotBgInactive);
            colorEdit(S::Get("HUD_Color_RingInactive", "Ring Inactive##slotringinactive").c_str(), st.slotRingInactive);
            u8Edit(S::Get("HUD_Color_RingActiveAlpha", "Ring Active Alpha##slotringactivealpha").c_str(),
                   st.slotRingActiveAlpha);
            u8Edit(S::Get("HUD_Color_IconAlpha", "Icon Alpha##iconalpha").c_str(), st.iconAlpha);
            colorEdit(S::Get("HUD_Color_EmptySlot", "Empty Slot##emptyslotcolor").c_str(), st.emptySlotColor);

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText(S::Get("HUD_Colors_RingCenter", "Ring Center").c_str());
            colorEdit(S::Get("HUD_Color_RingCenterFill", "Fill##ringcenterfill").c_str(), st.ringCenterFill);
            colorEdit(S::Get("HUD_Color_RingCenterBorder", "Border##ringcenterborder").c_str(), st.ringCenterBorder);

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText(S::Get("HUD_Colors_Schools", "School Colors").c_str());

            if (ImGuiMCP::BeginTable("##schoolcolors", 2, ImGuiMCP::ImGuiTableFlags_BordersInnerV)) {
                ImGuiMCP::TableSetupColumn(S::Get("HUD_Colors_Fill", "Fill").c_str(),
                                           ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableSetupColumn(S::Get("HUD_Colors_Glow", "Glow").c_str(),
                                           ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableHeadersRow();

                auto schoolRow = [&](std::string_view nameKey, std::string_view nameFallback, std::uint32_t& fill,
                                     std::uint32_t& glow) {
                    const std::string name = S::Get(nameKey, nameFallback);
                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableSetColumnIndex(0);
                    colorEdit((name + "##f").c_str(), fill);
                    ImGuiMCP::TableSetColumnIndex(1);
                    colorEdit((name + "##g").c_str(), glow);
                };

                schoolRow("HUD_School_Alteration", "Alteration", st.alterationFill, st.alterationGlow);
                schoolRow("HUD_School_Conjuration", "Conjuration", st.conjurationFill, st.conjurationGlow);
                schoolRow("HUD_School_Destruction", "Destruction", st.destructionFill, st.destructionGlow);
                schoolRow("HUD_School_Illusion", "Illusion", st.illusionFill, st.illusionGlow);
                schoolRow("HUD_School_Restoration", "Restoration", st.restorationFill, st.restorationGlow);
                schoolRow("HUD_School_Default", "Default", st.defaultFill, st.defaultGlow);
                schoolRow("HUD_School_Empty", "Empty", st.emptyFill, st.emptyFill);

                ImGuiMCP::EndTable();
            }
            ImGuiMCP::Spacing();
        }
    }

    void DrawPatchesTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        if (bool v1 = cfg.skipEquipAnimationPatch; ImGuiMCP::Checkbox(
                IntegratedMagic::Strings::Get("Item_SkipEquipAnim", "Skip equip animation").c_str(), &v1)) {
            cfg.skipEquipAnimationPatch = v1;
            dirty = true;
        }
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_SkipEquipAnim",
                                                    "Skips the equip animation when activating a slot via hotkey.\n"
                                                    "Recommended for faster spell swapping.")
                          .c_str());
        }

        if (bool v2 = cfg.skipEquipAnimationOnReturnPatch; ImGuiMCP::Checkbox(
                IntegratedMagic::Strings::Get("Item_SkipEquipAnimReturn", "Skip equip animation on return").c_str(),
                &v2)) {
            cfg.skipEquipAnimationOnReturnPatch = v2;
            dirty = true;
        }
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_SkipEquipAnimReturn",
                                                    "Skips the equip animation when restoring the previous snapshot\n"
                                                    "After exiting a slot.")
                          .c_str());
        }

        if (bool v3 = cfg.requireExclusiveHotkeyPatch; ImGuiMCP::Checkbox(
                IntegratedMagic::Strings::Get("Item_RequireExclusiveHotkey", "Require exclusive hotkey").c_str(),
                &v3)) {
            cfg.requireExclusiveHotkeyPatch = v3;
            dirty = true;
        }

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
                "%s", IntegratedMagic::Strings::Get("Tooltip_RequireExclusiveHotkey",
                                                    "When enabled, a hotkey combo only triggers if no other keys\n"
                                                    "are held at the same time. Prevents accidental activation\n"
                                                    "during combat or movement.")
                          .c_str());
        }

        if (bool v4 = cfg.pressBothAtSamePatch; ImGuiMCP::Checkbox(
                IntegratedMagic::Strings::Get("Item_PressBothAtSame", "Press both at the same time").c_str(), &v4)) {
            cfg.pressBothAtSamePatch = v4;
            dirty = true;
        }
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(
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
        ImGuiMCP::GetContentRegionAvail(&region);
        const float rightEdge = ImGuiMCP::GetCursorPosX() + region.x;
        ImGuiMCP::SetCursorPosX(rightEdge - kButtonWidth);
        ImGuiMCP::BeginDisabled(!g_pending);
        if (ImGuiMCP::Button(IntegratedMagic::Strings::Get("Item_Apply", "Apply changes").c_str(),
                             ImGuiMCP::ImVec2{kButtonWidth, 0.0f})) {
            cfg.Save();
            IntegratedMagic::StyleConfig::Get().Save();
            if (IntegratedMagic::SpellSettingsDB::Get().IsDirty()) {
                IntegratedMagic::SpellSettingsDB::Get().Save();
                IntegratedMagic::SpellSettingsDB::Get().ClearDirty();
            }
            Input::OnConfigChanged();
            g_pending = false;
        }
        ImGuiMCP::EndDisabled();
    }
    ImGuiMCP::Spacing();

    if (ImGuiMCP::BeginTabBar("IMAGIC_TABS")) {
        if (ImGuiMCP::BeginTabItem(IntegratedMagic::Strings::Get("Tab_General", "General").c_str())) {
            DrawGeneralTab(cfg, dirty);
            ImGuiMCP::EndTabItem();
        }
        if (ImGuiMCP::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Controls", "Controls").c_str())) {
            DrawControlsTab(cfg, dirty);
            ImGuiMCP::EndTabItem();
        }
        if (ImGuiMCP::BeginTabItem(IntegratedMagic::Strings::Get("Tab_HUD", "HUD").c_str())) {
            DrawHudTab(dirty);
            ImGuiMCP::EndTabItem();
        }
        if (ImGuiMCP::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Patches", "Patches").c_str())) {
            DrawPatchesTab(cfg, dirty);
            ImGuiMCP::EndTabItem();
        }
        ImGuiMCP::EndTabBar();
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