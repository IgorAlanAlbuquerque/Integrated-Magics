#include "UI_IntegratedMagic.h"

#include <array>
#include <cstdint>
#include <format>

#include "MagicConfig.h"
#include "MagicInput.h"
#include "MagicStrings.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "SpellSettingsDB.h"

static const char* GetSpellNameByFormID(std::uint32_t formID) {
    if (formID == 0) {
        return "None";
    }

    auto const* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
    if (!spell) {
        return "Invalid/Not loaded";
    }

    const char* name = spell->GetName();
    return (name && name[0] != '\0') ? name : "<unnamed>";
}

namespace ImGui = ImGuiMCP;

namespace {
    bool g_pending = false;          // NOSONAR
    bool g_capturingHotkey = false;  // NOSONAR
    int g_captureSlot = -1;          // NOSONAR

    IntegratedMagic::InputConfig& SlotInput(IntegratedMagic::MagicConfig& cfg, int slot) {
        if (slot == 0) return cfg.Magic1Input;
        if (slot == 1) return cfg.Magic2Input;
        if (slot == 2) return cfg.Magic3Input;
        return cfg.Magic4Input;
    }

    std::atomic<std::uint32_t>& SlotSpell(IntegratedMagic::MagicConfig& cfg, int slot) {
        if (slot == 0) return cfg.slotSpellFormID1;
        if (slot == 1) return cfg.slotSpellFormID2;
        if (slot == 2) return cfg.slotSpellFormID3;
        return cfg.slotSpellFormID4;
    }

    void DrawInputConfig(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        auto& icfg = SlotInput(cfg, slot);

        ImGui::PushID(slot);

        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_KeyboardKey", "Keyboard keys (scan codes)").c_str());

        int k1 = icfg.KeyboardScanCode1.load(std::memory_order_relaxed);
        int k2 = icfg.KeyboardScanCode2.load(std::memory_order_relaxed);
        int k3 = icfg.KeyboardScanCode3.load(std::memory_order_relaxed);

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Key1", "Key 1").c_str(), &k1)) {
            if (k1 < -1) k1 = -1;
            icfg.KeyboardScanCode1.store(k1, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Key2", "Key 2").c_str(), &k2)) {
            if (k2 < -1) k2 = -1;
            icfg.KeyboardScanCode2.store(k2, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Key3", "Key 3").c_str(), &k3)) {
            if (k3 < -1) k3 = -1;
            icfg.KeyboardScanCode3.store(k3, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::Spacing();

        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_GamepadButton", "Gamepad buttons").c_str());

        int g1 = icfg.GamepadButton1.load(std::memory_order_relaxed);
        int g2 = icfg.GamepadButton2.load(std::memory_order_relaxed);
        int g3 = icfg.GamepadButton3.load(std::memory_order_relaxed);

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Btn1", "Btn 1").c_str(), &g1)) {
            if (g1 < -1) g1 = -1;
            icfg.GamepadButton1.store(g1, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Btn2", "Btn 2").c_str(), &g2)) {
            if (g2 < -1) g2 = -1;
            icfg.GamepadButton2.store(g2, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_Btn3", "Btn 3").c_str(), &g3)) {
            if (g3 < -1) g3 = -1;
            icfg.GamepadButton3.store(g3, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::Spacing();

        if (!g_capturingHotkey || g_captureSlot != slot) {
            if (ImGui::Button(
                    IntegratedMagic::Strings::Get("Item_CaptureHotkey", "Capture next key/button after press esc")
                        .c_str())) {
                g_capturingHotkey = true;
                g_captureSlot = slot;
                MagicInput::RequestHotkeyCapture();
            }
        } else {
            ImGui::TextDisabled("%s", IntegratedMagic::Strings::Get(
                                          "Item_CaptureHotkey_Waiting",
                                          "Press ESC to close the menu then press a keyboard key or gamepad button...")
                                          .c_str());

            const int encoded = MagicInput::PollCapturedHotkey();
            if (encoded != -1) {
                if (encoded >= 0) {
                    icfg.KeyboardScanCode1.store(encoded, std::memory_order_relaxed);
                    icfg.KeyboardScanCode2.store(-1, std::memory_order_relaxed);
                    icfg.KeyboardScanCode3.store(-1, std::memory_order_relaxed);
                } else {
                    const int btn = -(encoded + 1);
                    icfg.GamepadButton1.store(btn, std::memory_order_relaxed);
                    icfg.GamepadButton2.store(-1, std::memory_order_relaxed);
                    icfg.GamepadButton3.store(-1, std::memory_order_relaxed);
                }

                dirty = true;
                g_capturingHotkey = false;
                g_captureSlot = -1;
            }
        }

        ImGui::PopID();
    }

    void DrawGeneralTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Hotkeys_Title", "Hotkeys").c_str());

        for (int slot = 0; slot < 4; slot++) {
            const auto title = IntegratedMagic::Strings::Get(std::format("Hotkeys_Magic{}", slot + 1),
                                                             std::format("Magic {} hotkeys", slot + 1));

            ImGui::Separator();
            ImGui::TextUnformatted(title.c_str());
            DrawInputConfig(cfg, slot, dirty);
        }
    }

    void DrawMagicTab(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        auto const& spellForm = SlotSpell(cfg, slot);
        const std::uint32_t formID = spellForm.load(std::memory_order_relaxed);

        ImGui::Text("%s: %s", "Selected Spell", GetSpellNameByFormID(formID));
        ImGui::Text("FormID: 0x%08X", formID);

        ImGui::Separator();

        if (formID == 0) {
            ImGui::TextDisabled("%s",
                                IntegratedMagic::Strings::Get("Spell_NoSpell", "No spell set for this slot.").c_str());
            return;
        }

        auto s = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(formID);

        const auto mHold = IntegratedMagic::Strings::Get("Item_Mode_Hold", "Hold");
        const auto mPress = IntegratedMagic::Strings::Get("Item_Mode_Press", "Press");
        const auto mAuto = IntegratedMagic::Strings::Get("Item_Mode_Automatic", "Automatic");
        const char* modes[] = {mHold.c_str(), mPress.c_str(), mAuto.c_str()};

        int modeIdx = (s.mode == IntegratedMagic::ActivationMode::Hold)    ? 0
                      : (s.mode == IntegratedMagic::ActivationMode::Press) ? 1
                                                                           : 2;

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(IntegratedMagic::Strings::Get("Item_Mode", "Activation mode").c_str(), &modeIdx, modes, 3)) {
            using enum IntegratedMagic::ActivationMode;
            s.mode = (modeIdx == 0) ? Hold : (modeIdx == 1) ? Press : Automatic;
            IntegratedMagic::SpellSettingsDB::Get().Set(formID, s);
            dirty = true;
        }

        const auto hLeft = IntegratedMagic::Strings::Get("Item_Hand_Left", "Left");
        const auto hRight = IntegratedMagic::Strings::Get("Item_Hand_Right", "Right");
        const auto hBoth = IntegratedMagic::Strings::Get("Item_Hand_Both", "Both");
        const char* hands[] = {hLeft.c_str(), hRight.c_str(), hBoth.c_str()};

        int handIdx = (s.hand == IntegratedMagic::EquipHand::Left)    ? 0
                      : (s.hand == IntegratedMagic::EquipHand::Right) ? 1
                                                                      : 2;

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(IntegratedMagic::Strings::Get("Item_Hand", "Equip hand").c_str(), &handIdx, hands, 3)) {
            using enum IntegratedMagic::EquipHand;
            s.hand = (handIdx == 0) ? Left : (handIdx == 1) ? Right : Both;
            IntegratedMagic::SpellSettingsDB::Get().Set(formID, s);
            dirty = true;
        }

        const bool disableAA = (s.mode != IntegratedMagic::ActivationMode::Hold);
        if (disableAA) ImGui::BeginDisabled(true);

        if (bool aa = s.autoAttack; ImGui::Checkbox(
                IntegratedMagic::Strings::Get("Item_AutoAttack", "Auto-attack while holding").c_str(), &aa)) {
            s.autoAttack = aa;
            IntegratedMagic::SpellSettingsDB::Get().Set(formID, s);
            dirty = true;
        }

        if (disableAA) {
            ImGui::EndDisabled();
        }
    }

    void DrawPatchesTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        bool v = cfg.skipEquipAnimationPatch;
        if (ImGui::Checkbox(
                IntegratedMagic::Strings::Get("Item_SkipEquipAnim", "Skip equip animation (global)").c_str(), &v)) {
            cfg.skipEquipAnimationPatch = v;
            dirty = true;
        }
    }
}

void __stdcall IntegratedMagic_UI::DrawSettings() {
    auto& cfg = IntegratedMagic::GetMagicConfig();

    bool dirty = false;

    if (ImGui::BeginTabBar("IMAGIC_TABS")) {
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_General", "General").c_str())) {
            DrawGeneralTab(cfg, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Magic1", "Magic 1").c_str())) {
            DrawMagicTab(cfg, 0, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Magic2", "Magic 2").c_str())) {
            DrawMagicTab(cfg, 1, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Magic3", "Magic 3").c_str())) {
            DrawMagicTab(cfg, 2, dirty);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(IntegratedMagic::Strings::Get("Tab_Magic4", "Magic 4").c_str())) {
            DrawMagicTab(cfg, 3, dirty);
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

    if (g_pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", IntegratedMagic::Strings::Get("Item_Pending", "(pending)").c_str());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!g_pending);
    if (ImGui::Button(IntegratedMagic::Strings::Get("Item_Apply", "Apply changes").c_str(),
                      ImGui::ImVec2{160.0f, 0.0f})) {
        cfg.Save();

        if (IntegratedMagic::SpellSettingsDB::Get().IsDirty()) {
            IntegratedMagic::SpellSettingsDB::Get().Save();
            IntegratedMagic::SpellSettingsDB::Get().ClearDirty();
        }
        MagicInput::OnConfigChanged();

        g_pending = false;
    }
    ImGui::EndDisabled();
}

void IntegratedMagic_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }

    SKSEMenuFramework::SetSection(IntegratedMagic::Strings::Get("SectionName", "Integrated Magic"));
    SKSEMenuFramework::AddSectionItem(IntegratedMagic::Strings::Get("SectionItem_Settings", "Settings"), DrawSettings);
}
