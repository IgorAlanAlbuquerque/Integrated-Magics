#pragma once
#include <optional>
#include "PCH.h"

namespace IntegratedMagic {
    struct SlotMutation {
        int slot{-1};
        std::optional<RE::FormID> leftSpell;
        std::optional<RE::FormID> rightSpell;
        std::optional<RE::FormID> shout;
    };
}
