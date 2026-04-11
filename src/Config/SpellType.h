#pragma once
#include <cstdint>

namespace IntegratedMagic {
    enum class SpellType : std::uint8_t {
        Unknown = 0,
        Concentration,
        Cast,
        Bound,
        Power,
        Shout,
    };

    enum class ActivationMode : std::uint32_t { Hold = 0, Press = 1, Automatic = 2 };
}