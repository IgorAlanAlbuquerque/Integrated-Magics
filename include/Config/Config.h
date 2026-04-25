#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "Config/Limits.h"
#include "Shared/SpellType.h"

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
        std::atomic<std::uint32_t> slotCount{4};
        std::array<InputConfig, IntegratedMagic::Config::kMaxSlots> slotInput;
        InputConfig hudPopupInput;

        std::array<SpellTypeDefaults, static_cast<std::size_t>(std::to_underlying(SpellType::Shout)) + 1>
            spellTypeDefaults{};

        std::byte hudVisibilityFlags{static_cast<std::byte>(std::to_underlying(HudVisibilityFlag::Always))};

        bool skipEquipAnimationPatch{false};
        bool skipEquipAnimationOnReturnPatch{false};
        bool requireExclusiveHotkeyPatch{false};
        bool pressBothAtSamePatch{false};

        int modifierKeyboardPosition{0};
        int modifierGamepadPosition{0};

        MagicConfig();
        void Load();
        void Save() const;
        [[nodiscard]] std::uint32_t SlotCount() const noexcept;

        [[nodiscard]] bool HudFlagSet(HudVisibilityFlag f) const noexcept {
            return (hudVisibilityFlags & static_cast<std::byte>(std::to_underlying(f))) != std::byte{0};
        }

    private:
        static std::filesystem::path IniPath();
    };

    [[nodiscard]] MagicConfig& GetMagicConfig();
}