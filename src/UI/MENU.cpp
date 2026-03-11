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

    void DrawKeyValueCapture(std::atomic<int>& field, bool wantKeyboard, bool& dirty) {
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

        ImGui::PopID();
    }

    void DrawInputDetailRow(const char* kbLabel, std::atomic<int>& kbField, const char* gpLabel,
                            std::atomic<int>& gpField, bool& dirty) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(kbLabel);

        ImGui::TableSetColumnIndex(1);
        DrawKeyValueCapture(kbField, true, dirty);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(gpLabel);

        ImGui::TableSetColumnIndex(3);
        DrawKeyValueCapture(gpField, false, dirty);
    }

    void DrawDetailPanel(IntegratedMagic::InputConfig& icfg, const char* title, bool& dirty) {
        ImGui::TextUnformatted(title);
        ImGui::Separator();
        ImGui::Spacing();

        if (const ImGuiMCP::ImGuiTableFlags kTableFlags =
                ImGuiMCP::ImGuiTableFlags_BordersOuter | ImGuiMCP::ImGuiTableFlags_BordersInnerV |
                ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_SizingFixedFit;
            ImGui::BeginTable("##InputDetail", 4, kTableFlags)) {
            ImGui::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Keyboard", "Keyboard").c_str(),
                                    ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("##kbv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn(IntegratedMagic::Strings::Get("Col_Gamepad", "Gamepad").c_str(),
                                    ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("##gpv", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableHeadersRow();

            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key1", "Key 1").c_str(), icfg.KeyboardScanCode1,
                               IntegratedMagic::Strings::Get("Item_Btn1", "Btn 1").c_str(), icfg.GamepadButton1, dirty);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key2", "Key 2").c_str(), icfg.KeyboardScanCode2,
                               IntegratedMagic::Strings::Get("Item_Btn2", "Btn 2").c_str(), icfg.GamepadButton2, dirty);
            DrawInputDetailRow(IntegratedMagic::Strings::Get("Item_Key3", "Key 3").c_str(), icfg.KeyboardScanCode3,
                               IntegratedMagic::Strings::Get("Item_Btn3", "Btn 3").c_str(), icfg.GamepadButton3, dirty);

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
                            dirty);
        } else if (g_selectedSlot >= 0 && g_selectedSlot < n) {
            const auto title = IntegratedMagic::Strings::Get(std::format("Detail_Magic{}", g_selectedSlot + 1),
                                                             std::format("Magic {}", g_selectedSlot + 1));
            DrawDetailPanel(cfg.slotInput[static_cast<std::size_t>(g_selectedSlot)], title.c_str(), dirty);
        }

        ImGui::EndChild();
    }

    void DrawHudTab(IntegratedMagic::MagicConfig&, bool&) {
        ImGui::TextDisabled("%s",
                            IntegratedMagic::Strings::Get("HUD_Placeholder", "HUD settings coming soon.").c_str());
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
            DrawHudTab(cfg, dirty);
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