#include "MagicConfig.h"

#include <SimpleIni.h>

#include <algorithm>
#include <format>
#include <string>

#include "MagicConfigPath.h"
#include "PCH.h"

using namespace std::string_literals;

namespace {
    int _getInt(CSimpleIniA& ini, const char* sec, const char* k, int defVal) {
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

    bool _getBool(CSimpleIniA& ini, const char* sec, const char* k, bool defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) {
            return defVal;
        }
        return (_stricmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
    }

    void _loadInput(CSimpleIniA& ini, const char* sec, IntegratedMagic::InputConfig& out) {
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
        for (auto& a : slotSpellFormID) {
            a.store(0u, std::memory_order_relaxed);
        }
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

        {
            const int raw = _getInt(ini, "General", "SlotCount", 4);
            std::uint32_t v = (raw < 1) ? 1u : static_cast<std::uint32_t>(raw);
            if (v > kMaxSlots) {
                v = kMaxSlots;
            }
            slotCount.store(v, std::memory_order_relaxed);
        }

        const auto n = SlotCount();

        for (std::uint32_t i = 0; i < n; ++i) {
            const auto sec = std::format("Magic{}", i + 1);
            _loadInput(ini, sec.c_str(), slotInput[i]);
        }

        skipEquipAnimationPatch = _getBool(ini, "Patches", "SkipEquipAnimationPatch", false);
        requireExclusiveHotkeyPatch = _getBool(ini, "Patches", "RequireExclusiveHotkeyPatch", false);
    }

    void MagicConfig::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();

        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        const auto n = SlotCount();

        ini.SetLongValue("General", "SlotCount", static_cast<long>(n));

        for (std::uint32_t i = 0; i < n; ++i) {
            const auto sec = std::format("Magic{}", i + 1);
            _saveInput(ini, sec.c_str(), slotInput[i]);
        }

        ini.SetBoolValue("Patches", "SkipEquipAnimationPatch", skipEquipAnimationPatch);
        ini.SetBoolValue("Patches", "RequireExclusiveHotkeyPatch", requireExclusiveHotkeyPatch);

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    MagicConfig& GetMagicConfig() {
        static MagicConfig g{};  // NOSONAR
        return g;
    }
}