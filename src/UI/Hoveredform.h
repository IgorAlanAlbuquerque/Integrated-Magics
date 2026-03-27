#pragma once

#include "PCH.h"

namespace IntegratedMagic::HoveredForm {

    enum class MagicType {
        None,
        Spell,
        TwoHandedSpell,
        RightOnlySpell,
        LeftOnlySpell,
        Shout,
        Power,
    };

    [[nodiscard]] RE::FormID GetHoveredFormID();

    [[nodiscard]] MagicType GetHoveredMagicType();
}