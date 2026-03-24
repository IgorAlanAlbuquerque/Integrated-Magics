#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "Persistence/SpellSettingsDB.h"
#include "SpellType.h"

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

    enum class HudVisibilityFlag : std::uint8_t {
        Never = 0,
        SlotActive = 1 << 0,
        InCombat = 1 << 1,
        WeaponDrawn = 1 << 2,
        Always = 1 << 3,
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
        std::uint8_t hudVisibilityFlags{static_cast<std::uint8_t>(HudVisibilityFlag::Always)};
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

        bool HudFlagSet(HudVisibilityFlag f) const noexcept {
            return (hudVisibilityFlags & static_cast<std::uint8_t>(f)) != 0;
        }

    private:
        static std::filesystem::path IniPath();
    };

    MagicConfig& GetMagicConfig();
}