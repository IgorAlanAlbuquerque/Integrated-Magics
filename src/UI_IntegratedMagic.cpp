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
        return cfg.slotInput[static_cast<std::size_t>(slot)];
    }

    std::atomic<std::uint32_t>& SlotSpell(IntegratedMagic::MagicConfig& cfg, int slot,
                                          IntegratedMagic::MagicSlots::Hand hand) {
        if (hand == IntegratedMagic::MagicSlots::Hand::Left) {
            return cfg.slotSpellFormIDLeft[static_cast<std::size_t>(slot)];
        }
        return cfg.slotSpellFormIDRight[static_cast<std::size_t>(slot)];
    }

    inline int ModeToIndex(IntegratedMagic::ActivationMode m) {
        using enum IntegratedMagic::ActivationMode;
        switch (m) {
            case Hold:
                return 0;
            case Press:
                return 1;
            case Automatic:
                return 2;
        }
        return 2;
    }

    inline IntegratedMagic::ActivationMode IndexToMode(int idx) {
        using enum IntegratedMagic::ActivationMode;
        switch (idx) {
            case 0:
                return Hold;
            case 1:
                return Press;
            default:
                return Automatic;
        }
    }

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

    inline void DrawKeyboardKeysUI(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        auto& icfg = SlotInput(cfg, slot);

        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_KeyboardKey", "Keyboard keys (scan codes)").c_str());

        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Key1", "Key 1"), icfg.KeyboardScanCode1, dirty);
        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Key2", "Key 2"), icfg.KeyboardScanCode2, dirty);
        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Key3", "Key 3"), icfg.KeyboardScanCode3, dirty);
    }

    inline void DrawGamepadButtonsUI(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        auto& icfg = SlotInput(cfg, slot);

        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_GamepadButton", "Gamepad buttons").c_str());

        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Btn1", "Btn 1"), icfg.GamepadButton1, dirty);
        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Btn2", "Btn 2"), icfg.GamepadButton2, dirty);
        DrawAtomicIntInput(IntegratedMagic::Strings::Get("Item_Btn3", "Btn 3"), icfg.GamepadButton3, dirty);
    }

    inline void DrawCaptureHotkeyUI(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        auto& icfg = SlotInput(cfg, slot);

        if (!g_capturingHotkey || g_captureSlot != slot) {
            if (ImGui::Button(
                    IntegratedMagic::Strings::Get("Item_CaptureHotkey", "Capture next key/button after press esc")
                        .c_str())) {
                g_capturingHotkey = true;
                g_captureSlot = slot;
                MagicInput::RequestHotkeyCapture();
            }
            return;
        }

        ImGui::TextDisabled("%s", IntegratedMagic::Strings::Get(
                                      "Item_CaptureHotkey_Waiting",
                                      "Press ESC to close the menu then press a keyboard key or gamepad button...")
                                      .c_str());

        const int encoded = MagicInput::PollCapturedHotkey();
        if (encoded == -1) {
            return;
        }

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

    void DrawInputConfig(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        ImGui::PushID(slot);

        DrawKeyboardKeysUI(cfg, slot, dirty);
        ImGui::Spacing();

        DrawGamepadButtonsUI(cfg, slot, dirty);
        ImGui::Spacing();

        DrawCaptureHotkeyUI(cfg, slot, dirty);

        ImGui::PopID();
    }

    void DrawOneSpellSettings(const char* title, std::uint32_t formID, bool& dirty) {
        ImGui::Text("%s: %s", title, GetSpellNameByFormID(formID));
        ImGui::Text("FormID: 0x%08X", formID);

        if (formID == 0u) {
            ImGui::TextDisabled("%s",
                                IntegratedMagic::Strings::Get("Spell_NoSpell", "No spell set for this slot.").c_str());
            return;
        }

        auto s = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(formID);

        const auto mHold = IntegratedMagic::Strings::Get("Item_Mode_Hold", "Hold");
        const auto mPress = IntegratedMagic::Strings::Get("Item_Mode_Press", "Press");
        const auto mAuto = IntegratedMagic::Strings::Get("Item_Mode_Automatic", "Automatic");
        const std::array<const char*, 3> modes{mHold.c_str(), mPress.c_str(), mAuto.c_str()};

        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_ModeLabel", "Activation mode").c_str());
        int modeIdx = ModeToIndex(s.mode);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##mode", &modeIdx, modes.data(), static_cast<int>(modes.size()))) {
            s.mode = IndexToMode(modeIdx);
            IntegratedMagic::SpellSettingsDB::Get().Set(formID, s);
            dirty = true;
        }

        const bool disableAA = (s.mode != IntegratedMagic::ActivationMode::Hold);
        if (disableAA) ImGui::BeginDisabled(true);

        if (bool aa = s.autoAttack; ImGui::Checkbox("##aa", &aa)) {
            s.autoAttack = aa;
            IntegratedMagic::SpellSettingsDB::Get().Set(formID, s);
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Item_AutoAttack", "Auto-attack while holding").c_str());

        if (disableAA) ImGui::EndDisabled();
    }

    void DrawGeneralTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        ImGui::TextUnformatted(IntegratedMagic::Strings::Get("Hotkeys_Title", "Hotkeys").c_str());

        auto n = static_cast<int>(cfg.SlotCount());
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputInt(IntegratedMagic::Strings::Get("Item_SlotCount", "Slot count").c_str(), &n)) {
            if (n < 1) n = 1;
            if (n > static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots)) {
                n = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
            }
            cfg.slotCount.store(static_cast<std::uint32_t>(n), std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();

        n = static_cast<int>(cfg.SlotCount());
        for (int slot = 0; slot < n; ++slot) {
            const auto title = IntegratedMagic::Strings::Get(std::format("Hotkeys_Magic{}", slot + 1),
                                                             std::format("Magic {} hotkeys", slot + 1));

            ImGui::Separator();
            ImGui::TextUnformatted(title.c_str());
            DrawInputConfig(cfg, slot, dirty);
        }
    }

    void DrawMagicTab(IntegratedMagic::MagicConfig& cfg, int slot, bool& dirty) {
        const std::uint32_t rightID =
            SlotSpell(cfg, slot, IntegratedMagic::MagicSlots::Hand::Right).load(std::memory_order_relaxed);
        const std::uint32_t leftID =
            SlotSpell(cfg, slot, IntegratedMagic::MagicSlots::Hand::Left).load(std::memory_order_relaxed);

        if (ImGui::BeginTable("SlotSpellsTable", 2)) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID("RightSpell");
            DrawOneSpellSettings(IntegratedMagic::Strings::Get("Item_Spell_Right", "Right-hand spell").c_str(), rightID,
                                 dirty);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID("LeftSpell");
            DrawOneSpellSettings(IntegratedMagic::Strings::Get("Item_Spell_Left", "Left-hand spell").c_str(), leftID,
                                 dirty);
            ImGui::PopID();

            ImGui::EndTable();
        }
    }

    void DrawPatchesTab(IntegratedMagic::MagicConfig& cfg, bool& dirty) {
        {
            bool v = cfg.skipEquipAnimationPatch;
            if (ImGui::Checkbox(IntegratedMagic::Strings::Get("Item_SkipEquipAnim", "Skip equip animation").c_str(),
                                &v)) {
                cfg.skipEquipAnimationPatch = v;
                dirty = true;
            }
        }

        {
            bool v = cfg.requireExclusiveHotkeyPatch;
            if (ImGui::Checkbox(
                    IntegratedMagic::Strings::Get("Item_RequireExclusiveHotkey", "Require exclusive hotkey").c_str(),
                    &v)) {
                cfg.requireExclusiveHotkeyPatch = v;
                dirty = true;
            }
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
        const auto n = static_cast<int>(cfg.SlotCount());
        for (int slot = 0; slot < n; ++slot) {
            const auto title =
                IntegratedMagic::Strings::Get(std::format("Tab_Magic{}", slot + 1), std::format("Magic {}", slot + 1));

            if (ImGui::BeginTabItem(title.c_str())) {
                DrawMagicTab(cfg, slot, dirty);
                ImGui::EndTabItem();
            }
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