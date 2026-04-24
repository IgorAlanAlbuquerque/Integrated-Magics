#pragma once
#include <cstdint>

namespace IntegratedMagic::Config {

    enum class HudVisibilityFlag : std::uint8_t {
        Never = 0,
        SlotActive = 1 << 0,
        InCombat = 1 << 1,
        WeaponDrawn = 1 << 2,
        Always = 1 << 3,
    };

    class IHudSettings {
    public:
        virtual ~IHudSettings() = default;

        [[nodiscard]] virtual bool FlagSet(HudVisibilityFlag f) const = 0;
        [[nodiscard]] virtual int SlotCount() const = 0;
    };
}