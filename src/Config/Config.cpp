#include "Config.h"

#include <SimpleIni.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "PCH.h"
#include "Util/ConfigPath.h"

using namespace std::string_literals;

namespace {

    int _getInt(CSimpleIniA const& ini, const char* sec, const char* k, int defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        const long r = std::strtol(v, &end, 10);
        if (!end || end == v) return defVal;
        return static_cast<int>(r);
    }

    bool _getBool(CSimpleIniA const& ini, const char* sec, const char* k, bool defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
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

    struct SpellTypeEntry {
        IntegratedMagic::SpellType type;
        const char* modeKey;
        const char* aaKey;
    };

    constexpr std::array kSpellTypeEntries{
        SpellTypeEntry{IntegratedMagic::SpellType::Concentration, "ConcentrationMode", "ConcentrationAutoAttack"},
        SpellTypeEntry{IntegratedMagic::SpellType::Cast, "CastMode", "CastAutoAttack"},
        SpellTypeEntry{IntegratedMagic::SpellType::Bound, "BoundMode", "BoundAutoAttack"},
        SpellTypeEntry{IntegratedMagic::SpellType::Power, "PowerMode", "PowerAutoAttack"},
        SpellTypeEntry{IntegratedMagic::SpellType::Shout, "ShoutMode", "ShoutAutoAttack"},
    };

    struct HudFlagEntry {
        IntegratedMagic::HudVisibilityFlag flag;
        const char* key;
    };

    constexpr std::array kHudFlagEntries{
        HudFlagEntry{IntegratedMagic::HudVisibilityFlag::Always, "HudShowAlways"},
        HudFlagEntry{IntegratedMagic::HudVisibilityFlag::SlotActive, "HudShowOnSlotActive"},
        HudFlagEntry{IntegratedMagic::HudVisibilityFlag::InCombat, "HudShowInCombat"},
        HudFlagEntry{IntegratedMagic::HudVisibilityFlag::WeaponDrawn, "HudShowWeaponDrawn"},
    };

    using FieldPtr = std::atomic<int> IntegratedMagic::InputConfig::*;
    constexpr FieldPtr GetInputField(int pos, bool isKb) noexcept {
        if (isKb) {
            if (pos == 1) return &IntegratedMagic::InputConfig::KeyboardScanCode1;
            if (pos == 2) return &IntegratedMagic::InputConfig::KeyboardScanCode2;
            return &IntegratedMagic::InputConfig::KeyboardScanCode3;
        }

        if (pos == 1) return &IntegratedMagic::InputConfig::GamepadButton1;
        if (pos == 2) return &IntegratedMagic::InputConfig::GamepadButton2;
        return &IntegratedMagic::InputConfig::GamepadButton3;
    }
}

IntegratedMagic::MagicConfig::MagicConfig() {
    using enum IntegratedMagic::SpellType;
    using enum IntegratedMagic::ActivationMode;
    spellTypeDefaults[static_cast<int>(std::to_underlying(Concentration))] = {Hold, true};
    spellTypeDefaults[static_cast<int>(std::to_underlying(Cast))] = {Automatic, true};
    spellTypeDefaults[static_cast<int>(std::to_underlying(Bound))] = {Press, false};
    spellTypeDefaults[static_cast<int>(std::to_underlying(Power))] = {Automatic, false};
    spellTypeDefaults[static_cast<int>(std::to_underlying(Shout))] = {Hold, true};
    spellTypeDefaults[static_cast<int>(std::to_underlying(Unknown))] = {Hold, true};
}

std::filesystem::path IntegratedMagic::MagicConfig::IniPath() { return GetThisDllDir() / "IntegratedMagic.ini"; }

std::uint32_t IntegratedMagic::MagicConfig::SlotCount() const noexcept {
    auto v = slotCount.load(std::memory_order_relaxed);
    if (v < 1u) v = 1u;
    if (v > IntegratedMagic::Config::kMaxSlots) v = IntegratedMagic::Config::kMaxSlots;
    return v;
}

void IntegratedMagic::MagicConfig::Load() {
    CSimpleIniA ini;
    ini.SetUnicode();
    if (const auto path = IniPath(); ini.LoadFile(path.string().c_str()) < 0) return;

    const int raw = _getInt(ini, "General", "SlotCount", 4);
    std::uint32_t v = (raw < 1) ? 1u : static_cast<std::uint32_t>(raw);
    if (v > IntegratedMagic::Config::kMaxSlots) v = IntegratedMagic::Config::kMaxSlots;
    slotCount.store(v, std::memory_order_relaxed);

    std::byte flags{0};
    for (const auto& e : kHudFlagEntries) {
        if (_getBool(ini, "General", e.key, false)) {
            flags |= static_cast<std::byte>(std::to_underlying(e.flag));
        }
    }
    hudVisibilityFlags = flags;

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
    auto propagate = [&](int pos, bool isKb) {
        if (pos <= 0) return;

        const FieldPtr field = GetInputField(pos, isKb);
        const int canonical = (slotInput[0].*field).load(std::memory_order_relaxed);

        for (std::uint32_t i = 1; i < n; ++i) (slotInput[i].*field).store(canonical, std::memory_order_relaxed);
    };
    propagate(modifierKeyboardPosition, true);
    propagate(modifierGamepadPosition, false);

    constexpr const char* sec = "SpellTypeDefaults";
    for (const auto& e : kSpellTypeEntries) {
        auto& d = spellTypeDefaults[static_cast<int>(std::to_underlying(e.type))];
        d.mode = _modeFromStr(ini.GetValue(sec, e.modeKey, _modeToStr(d.mode)));
        d.autoAttack = _getBool(ini, sec, e.aaKey, d.autoAttack);
    }
}

void IntegratedMagic::MagicConfig::Save() const {
    CSimpleIniA ini;
    ini.SetUnicode();
    const auto path = IniPath();
    ini.LoadFile(path.string().c_str());

    const auto n = SlotCount();
    ini.SetLongValue("General", "SlotCount", static_cast<long>(n));

    for (const auto& e : kHudFlagEntries) {
        ini.SetBoolValue("General", e.key, HudFlagSet(e.flag));
    }

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

    constexpr const char* sec = "SpellTypeDefaults";
    for (const auto& e : kSpellTypeEntries) {
        const auto& d = spellTypeDefaults[static_cast<int>(std::to_underlying(e.type))];
        ini.SetValue(sec, e.modeKey, _modeToStr(d.mode));
        ini.SetBoolValue(sec, e.aaKey, d.autoAttack);
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    ini.SaveFile(path.string().c_str());
}

IntegratedMagic::MagicConfig& IntegratedMagic::GetMagicConfig() {
    static MagicConfig g{};  // NOSONAR
    return g;
}
