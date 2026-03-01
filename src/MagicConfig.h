#pragma once
#include <array>
#include <atomic>
#include <cstddef>
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
        static constexpr std::uint32_t kMaxSlots = 64;
        std::atomic<std::uint32_t> slotCount{4};
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotSpellFormIDLeft;
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotSpellFormIDRight;
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotShoutFormID;
        std::array<InputConfig, kMaxSlots> slotInput;
        bool skipEquipAnimationPatch = false;
        bool requireExclusiveHotkeyPatch = false;
        MagicConfig();
        void Load();
        void Save() const;
        std::uint32_t SlotCount() const noexcept;

    private:
        static std::filesystem::path IniPath();
    };

    MagicConfig& GetMagicConfig();
}