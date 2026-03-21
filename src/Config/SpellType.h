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

    SpellType DetectSpellType(const RE::TESForm* form);
}