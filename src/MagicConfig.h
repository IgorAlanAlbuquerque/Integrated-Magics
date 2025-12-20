#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>

namespace IntegratedMagic {
    struct InputConfig {
        std::atomic<int> KeyboardScanCode1{-1};
        std::atomic<int> KeyboardScanCode2{-1};
        std::atomic<int> KeyboardScanCode3{-1};

        std::atomic<int> GamepadButton1{-1};
        std::atomic<int> GamepadButton2{-1};
        std::atomic<int> GamepadButton3{-1};
    };

    struct MagicConfig {
        std::atomic<float> holdThresholdSeconds{0.20f};

        std::atomic<std::uint32_t> slotSpellFormID1{0};
        std::atomic<std::uint32_t> slotSpellFormID2{0};
        std::atomic<std::uint32_t> slotSpellFormID3{0};
        std::atomic<std::uint32_t> slotSpellFormID4{0};

        InputConfig Magic1Input;
        InputConfig Magic2Input;
        InputConfig Magic3Input;
        InputConfig Magic4Input;

        bool skipEquipAnimationPatch = false;

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    MagicConfig& GetMagicConfig();
}
