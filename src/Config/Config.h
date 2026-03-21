#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "SpellType.h"
#include "Persistence/SpellSettingsDB.h"

namespace IntegratedMagic {

    struct InputConfig {
        std::atomic<int> KeyboardScanCode1{-1};
        std::atomic<int> KeyboardScanCode2{-1};
        std::atomic<int> KeyboardScanCode3{-1};
        std::atomic<int> GamepadButton1{-1};
        std::atomic<int> GamepadButton2{-1};
        std::atomic<int> GamepadButton3{-1};
    };

    struct SpellTypeDefaults {
        ActivationMode mode{ActivationMode::Hold};
        bool autoAttack{true};
    };

    struct MagicConfig {
        static constexpr std::uint32_t kMaxSlots = 64;
        std::atomic<std::uint32_t> slotCount{4};
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotSpellFormIDLeft;
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotSpellFormIDRight;
        std::array<std::atomic<std::uint32_t>, kMaxSlots> slotShoutFormID;
        std::array<InputConfig, kMaxSlots> slotInput;
        InputConfig hudPopupInput;
        std::array<SpellTypeDefaults, static_cast<std::size_t>(SpellType::Shout) + 1> spellTypeDefaults{};
        bool hudVisible{true};
        bool skipEquipAnimationPatch = false;
        bool skipEquipAnimationOnReturnPatch = false;
        bool requireExclusiveHotkeyPatch = false;
        bool pressBothAtSamePatch = false;

        int modifierKeyboardPosition{0};
        int modifierGamepadPosition{0};
        MagicConfig();
        void Load();
        void Save() const;
        std::uint32_t SlotCount() const noexcept;

    private:
        static std::filesystem::path IniPath();
    };

    MagicConfig& GetMagicConfig();
}