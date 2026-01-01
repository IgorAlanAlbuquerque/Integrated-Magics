#include "MagicConfig.h"

#include <SimpleIni.h>

#include <string>

#include "MagicConfigPath.h"
#include "PCH.h"

using namespace std::string_literals;

namespace {
    int _getInt(CSimpleIniA& ini, const char* sec, const char* k, int defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        long r = std::strtol(v, &end, 10);
        if (!end || end == v) return defVal;
        return static_cast<int>(r);
    }

    float _getFloat(CSimpleIniA& ini, const char* sec, const char* k, float defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        double r = std::strtod(v, &end);
        if (!end || end == v) return defVal;
        return static_cast<float>(r);
    }

    bool _getBool(CSimpleIniA& ini, const char* sec, const char* k, bool defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        return (_stricmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
    }

    std::string _getStr(CSimpleIniA& ini, const char* sec, const char* k, const char* defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        return v ? std::string{v} : std::string{defVal};
    }
}

namespace IntegratedMagic {
    std::filesystem::path MagicConfig::IniPath() {
        const auto& base = GetThisDllDir();
        return base / "IntegratedMagic.ini";
    }

    void MagicConfig::Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        if (SI_Error rc = ini.LoadFile(path.string().c_str()); rc < 0) {
            return;
        }

        Magic1Input.KeyboardScanCode1.store(_getInt(ini, "Magic1", "KeyboardScanCode1", -1), std::memory_order_relaxed);
        Magic1Input.KeyboardScanCode2.store(_getInt(ini, "Magic1", "KeyboardScanCode2", -1), std::memory_order_relaxed);
        Magic1Input.KeyboardScanCode3.store(_getInt(ini, "Magic1", "KeyboardScanCode3", -1), std::memory_order_relaxed);
        Magic1Input.GamepadButton1.store(_getInt(ini, "Magic1", "GamepadButton1", -1), std::memory_order_relaxed);
        Magic1Input.GamepadButton2.store(_getInt(ini, "Magic1", "GamepadButton2", -1), std::memory_order_relaxed);
        Magic1Input.GamepadButton3.store(_getInt(ini, "Magic1", "GamepadButton3", -1), std::memory_order_relaxed);

        Magic2Input.KeyboardScanCode1.store(_getInt(ini, "Magic2", "KeyboardScanCode1", -1), std::memory_order_relaxed);
        Magic2Input.KeyboardScanCode2.store(_getInt(ini, "Magic2", "KeyboardScanCode2", -1), std::memory_order_relaxed);
        Magic2Input.KeyboardScanCode3.store(_getInt(ini, "Magic2", "KeyboardScanCode3", -1), std::memory_order_relaxed);
        Magic2Input.GamepadButton1.store(_getInt(ini, "Magic2", "GamepadButton1", -1), std::memory_order_relaxed);
        Magic2Input.GamepadButton2.store(_getInt(ini, "Magic2", "GamepadButton2", -1), std::memory_order_relaxed);
        Magic2Input.GamepadButton3.store(_getInt(ini, "Magic2", "GamepadButton3", -1), std::memory_order_relaxed);

        Magic3Input.KeyboardScanCode1.store(_getInt(ini, "Magic3", "KeyboardScanCode1", -1), std::memory_order_relaxed);
        Magic3Input.KeyboardScanCode2.store(_getInt(ini, "Magic3", "KeyboardScanCode2", -1), std::memory_order_relaxed);
        Magic3Input.KeyboardScanCode3.store(_getInt(ini, "Magic3", "KeyboardScanCode3", -1), std::memory_order_relaxed);
        Magic3Input.GamepadButton1.store(_getInt(ini, "Magic3", "GamepadButton1", -1), std::memory_order_relaxed);
        Magic3Input.GamepadButton2.store(_getInt(ini, "Magic3", "GamepadButton2", -1), std::memory_order_relaxed);
        Magic3Input.GamepadButton3.store(_getInt(ini, "Magic3", "GamepadButton3", -1), std::memory_order_relaxed);

        Magic4Input.KeyboardScanCode1.store(_getInt(ini, "Magic4", "KeyboardScanCode1", -1), std::memory_order_relaxed);
        Magic4Input.KeyboardScanCode2.store(_getInt(ini, "Magic4", "KeyboardScanCode2", -1), std::memory_order_relaxed);
        Magic4Input.KeyboardScanCode3.store(_getInt(ini, "Magic4", "KeyboardScanCode3", -1), std::memory_order_relaxed);
        Magic4Input.GamepadButton1.store(_getInt(ini, "Magic4", "GamepadButton1", -1), std::memory_order_relaxed);
        Magic4Input.GamepadButton2.store(_getInt(ini, "Magic4", "GamepadButton2", -1), std::memory_order_relaxed);
        Magic4Input.GamepadButton3.store(_getInt(ini, "Magic4", "GamepadButton3", -1), std::memory_order_relaxed);

        skipEquipAnimationPatch = _getBool(ini, "Patches", "SkipEquipAnimationPatch", false);
        requireExclusiveHotkeyPatch = _getBool(ini, "Patches", "RequireExclusiveHotkeyPatch", false);
    }

    void MagicConfig::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        ini.SetLongValue("Magic1", "KeyboardScanCode1", Magic1Input.KeyboardScanCode1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic1", "KeyboardScanCode2", Magic1Input.KeyboardScanCode2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic1", "KeyboardScanCode3", Magic1Input.KeyboardScanCode3.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic1", "GamepadButton1", Magic1Input.GamepadButton1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic1", "GamepadButton2", Magic1Input.GamepadButton2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic1", "GamepadButton3", Magic1Input.GamepadButton3.load(std::memory_order_relaxed));

        ini.SetLongValue("Magic2", "KeyboardScanCode1", Magic2Input.KeyboardScanCode1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic2", "KeyboardScanCode2", Magic2Input.KeyboardScanCode2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic2", "KeyboardScanCode3", Magic2Input.KeyboardScanCode3.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic2", "GamepadButton1", Magic2Input.GamepadButton1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic2", "GamepadButton2", Magic2Input.GamepadButton2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic2", "GamepadButton3", Magic2Input.GamepadButton3.load(std::memory_order_relaxed));

        ini.SetLongValue("Magic3", "KeyboardScanCode1", Magic3Input.KeyboardScanCode1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic3", "KeyboardScanCode2", Magic3Input.KeyboardScanCode2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic3", "KeyboardScanCode3", Magic3Input.KeyboardScanCode3.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic3", "GamepadButton1", Magic3Input.GamepadButton1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic3", "GamepadButton2", Magic3Input.GamepadButton2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic3", "GamepadButton3", Magic3Input.GamepadButton3.load(std::memory_order_relaxed));

        ini.SetLongValue("Magic4", "KeyboardScanCode1", Magic4Input.KeyboardScanCode1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic4", "KeyboardScanCode2", Magic4Input.KeyboardScanCode2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic4", "KeyboardScanCode3", Magic4Input.KeyboardScanCode3.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic4", "GamepadButton1", Magic4Input.GamepadButton1.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic4", "GamepadButton2", Magic4Input.GamepadButton2.load(std::memory_order_relaxed));
        ini.SetLongValue("Magic4", "GamepadButton3", Magic4Input.GamepadButton3.load(std::memory_order_relaxed));

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
