#include "Config.h"

#include <SimpleIni.h>

#include <algorithm>
#include <format>
#include <string>

#include "ConfigPath.h"
#include "PCH.h"

using namespace std::string_literals;

namespace {
    int _getInt(CSimpleIniA const& ini, const char* sec, const char* k, int defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) {
            return defVal;
        }
        char* end = nullptr;
        long r = std::strtol(v, &end, 10);
        if (!end || end == v) {
            return defVal;
        }
        return static_cast<int>(r);
    }

    bool _getBool(CSimpleIniA const& ini, const char* sec, const char* k, bool defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) {
            return defVal;
        }
        return (_stricmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
    }

    const char* _modeToStr(IntegratedMagic::ActivationMode m) {
        using enum IntegratedMagic::ActivationMode;
        switch (m) {
            case Press:
                return "Press";
            case Automatic:
                return "Automatic";
            case Hold:
            default:
                return "Hold";
        }
    }

    IntegratedMagic::ActivationMode _modeFromStr(const char* v) {
        using enum IntegratedMagic::ActivationMode;
        if (!v) return Hold;
        if (_stricmp(v, "Press") == 0) return Press;
        if (_stricmp(v, "Automatic") == 0) return Automatic;
        return Hold;
    }

    void _loadInput(CSimpleIniA const& ini, const char* sec, IntegratedMagic::InputConfig& out) {
        out.KeyboardScanCode1.store(_getInt(ini, sec, "KeyboardScanCode1", -1), std::memory_order_relaxed);
        out.KeyboardScanCode2.store(_getInt(ini, sec, "KeyboardScanCode2", -1), std::memory_order_relaxed);
        out.KeyboardScanCode3.store(_getInt(ini, sec, "KeyboardScanCode3", -1), std::memory_order_relaxed);
        out.GamepadButton1.store(_getInt(ini, sec, "GamepadButton1", -1), std::memory_order_relaxed);
        out.GamepadButton2.store(_getInt(ini, sec, "GamepadButton2", -1), std::memory_order_relaxed);
        out.GamepadButton3.store(_getInt(ini, sec, "GamepadButton3", -1), std::memory_order_relaxed);
    }

    void _saveInput(CSimpleIniA& ini, const char* sec, const IntegratedMagic::InputConfig& in) {
        ini.SetLongValue(sec, "KeyboardScanCode1", in.KeyboardScanCode1.load(std::memory_order_relaxed));
        ini.SetLongValue(sec, "KeyboardScanCode2", in.KeyboardScanCode2.load(std::memory_order_relaxed));
        ini.SetLongValue(sec, "KeyboardScanCode3", in.KeyboardScanCode3.load(std::memory_order_relaxed));
        ini.SetLongValue(sec, "GamepadButton1", in.GamepadButton1.load(std::memory_order_relaxed));
        ini.SetLongValue(sec, "GamepadButton2", in.GamepadButton2.load(std::memory_order_relaxed));
        ini.SetLongValue(sec, "GamepadButton3", in.GamepadButton3.load(std::memory_order_relaxed));
    }
}

namespace IntegratedMagic {

    MagicConfig::MagicConfig() {
        for (auto& a : slotSpellFormIDLeft) a.store(0u, std::memory_order_relaxed);
        for (auto& a : slotSpellFormIDRight) a.store(0u, std::memory_order_relaxed);
        using ST = SpellType;
        spellTypeDefaults[static_cast<int>(ST::Concentration)] = {ActivationMode::Hold, true};
        spellTypeDefaults[static_cast<int>(ST::Cast)] = {ActivationMode::Automatic, true};
        spellTypeDefaults[static_cast<int>(ST::Bound)] = {ActivationMode::Press, false};
        spellTypeDefaults[static_cast<int>(ST::Power)] = {ActivationMode::Automatic, false};
        spellTypeDefaults[static_cast<int>(ST::Shout)] = {ActivationMode::Hold, true};
        spellTypeDefaults[static_cast<int>(ST::Unknown)] = {ActivationMode::Hold, true};
    }

    std::filesystem::path MagicConfig::IniPath() {
        const auto& base = GetThisDllDir();
        return base / "IntegratedMagic.ini";
    }

    std::uint32_t MagicConfig::SlotCount() const noexcept {
        auto v = slotCount.load(std::memory_order_relaxed);
        if (v < 1u) {
            v = 1u;
        } else if (v > kMaxSlots) {
            v = kMaxSlots;
        }
        return v;
    }

    void MagicConfig::Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        if (SI_Error rc = ini.LoadFile(path.string().c_str()); rc < 0) {
            return;
        }
        const int raw = _getInt(ini, "General", "SlotCount", 4);
        using F = HudVisibilityFlag;
        std::uint8_t flags = 0;
        if (_getBool(ini, "General", "HudShowAlways", false)) flags |= static_cast<std::uint8_t>(F::Always);
        if (_getBool(ini, "General", "HudShowOnSlotActive", false)) flags |= static_cast<std::uint8_t>(F::SlotActive);
        if (_getBool(ini, "General", "HudShowInCombat", false)) flags |= static_cast<std::uint8_t>(F::InCombat);
        if (_getBool(ini, "General", "HudShowWeaponDrawn", false)) flags |= static_cast<std::uint8_t>(F::WeaponDrawn);
        hudVisibilityFlags = flags;
        std::uint32_t v = (raw < 1) ? 1u : static_cast<std::uint32_t>(raw);
        if (v > kMaxSlots) {
            v = kMaxSlots;
        }
        slotCount.store(v, std::memory_order_relaxed);
        const auto n = SlotCount();
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto sec = std::format("Magic{}", i + 1);
            _loadInput(ini, sec.c_str(), slotInput[i]);
        }
        _loadInput(ini, "HudPopup", hudPopupInput);
        skipEquipAnimationPatch = _getBool(ini, "Patches", "SkipEquipAnimationPatch", false);
        skipEquipAnimationOnReturnPatch = _getBool(ini, "Patches", "SkipEquipAnimationOnReturn", false);
        requireExclusiveHotkeyPatch = _getBool(ini, "Patches", "RequireExclusiveHotkeyPatch", false);
        pressBothAtSamePatch = _getBool(ini, "Patches", "PressBothAtSamePatch", false);

        modifierKeyboardPosition = std::clamp(_getInt(ini, "Modifier", "KeyboardPosition", 0), 0, 3);
        modifierGamepadPosition = std::clamp(_getInt(ini, "Modifier", "GamepadPosition", 0), 0, 3);

        using FieldPtr = std::atomic<int> InputConfig::*;

        auto propagateModifier = [&](int pos, bool isKb) {
            if (pos <= 0) return;
            FieldPtr field = nullptr;
            if (isKb) {
                if (pos == 1)
                    field = &InputConfig::KeyboardScanCode1;
                else if (pos == 2)
                    field = &InputConfig::KeyboardScanCode2;
                else
                    field = &InputConfig::KeyboardScanCode3;
            } else {
                if (pos == 1)
                    field = &InputConfig::GamepadButton1;
                else if (pos == 2)
                    field = &InputConfig::GamepadButton2;
                else
                    field = &InputConfig::GamepadButton3;
            }
            const int canonical = (slotInput[0].*field).load(std::memory_order_relaxed);
            for (std::uint32_t i = 1; i < n; ++i) (slotInput[i].*field).store(canonical, std::memory_order_relaxed);
        };

        propagateModifier(modifierKeyboardPosition, true);
        propagateModifier(modifierGamepadPosition, false);

        using ST = SpellType;
        constexpr const char* sec = "SpellTypeDefaults";

        struct TypeEntry {
            ST type;
            const char* modeKey;
            const char* aaKey;
        };
        constexpr TypeEntry entries[] = {
            {ST::Concentration, "ConcentrationMode", "ConcentrationAutoAttack"},
            {ST::Cast, "CastMode", "CastAutoAttack"},
            {ST::Bound, "BoundMode", "BoundAutoAttack"},
            {ST::Power, "PowerMode", "PowerAutoAttack"},
            {ST::Shout, "ShoutMode", "ShoutAutoAttack"},
        };

        for (const auto& e : entries) {
            auto& d = spellTypeDefaults[static_cast<int>(e.type)];
            const char* modeStr = ini.GetValue(sec, e.modeKey, _modeToStr(d.mode));
            d.mode = _modeFromStr(modeStr);
            d.autoAttack = _getBool(ini, sec, e.aaKey, d.autoAttack);
        }
    }

    void MagicConfig::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());
        const auto n = SlotCount();
        ini.SetLongValue("General", "SlotCount", static_cast<long>(n));
        using F = HudVisibilityFlag;
        ini.SetBoolValue("General", "HudShowAlways", HudFlagSet(F::Always));
        ini.SetBoolValue("General", "HudShowOnSlotActive", HudFlagSet(F::SlotActive));
        ini.SetBoolValue("General", "HudShowInCombat", HudFlagSet(F::InCombat));
        ini.SetBoolValue("General", "HudShowWeaponDrawn", HudFlagSet(F::WeaponDrawn));
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto sec = std::format("Magic{}", i + 1);
            _saveInput(ini, sec.c_str(), slotInput[i]);
        }
        _saveInput(ini, "HudPopup", hudPopupInput);
        ini.SetBoolValue("Patches", "SkipEquipAnimationPatch", skipEquipAnimationPatch);
        ini.SetBoolValue("Patches", "SkipEquipAnimationOnReturn", skipEquipAnimationOnReturnPatch);
        ini.SetBoolValue("Patches", "RequireExclusiveHotkeyPatch", requireExclusiveHotkeyPatch);
        ini.SetBoolValue("Patches", "PressBothAtSamePatch", pressBothAtSamePatch);
        ini.SetLongValue("Modifier", "KeyboardPosition", modifierKeyboardPosition);
        ini.SetLongValue("Modifier", "GamepadPosition", modifierGamepadPosition);

        using ST = SpellType;
        constexpr const char* sec = "SpellTypeDefaults";
        struct TypeEntry {
            ST type;
            const char* modeKey;
            const char* aaKey;
        };
        constexpr TypeEntry entries[] = {
            {ST::Concentration, "ConcentrationMode", "ConcentrationAutoAttack"},
            {ST::Cast, "CastMode", "CastAutoAttack"},
            {ST::Bound, "BoundMode", "BoundAutoAttack"},
            {ST::Power, "PowerMode", "PowerAutoAttack"},
            {ST::Shout, "ShoutMode", "ShoutAutoAttack"},
        };
        for (const auto& e : entries) {
            const auto& d = spellTypeDefaults[static_cast<int>(e.type)];
            ini.SetValue(sec, e.modeKey, _modeToStr(d.mode));
            ini.SetBoolValue(sec, e.aaKey, d.autoAttack);
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    MagicConfig& GetMagicConfig() {
        static MagicConfig g{};
        return g;
    }
}