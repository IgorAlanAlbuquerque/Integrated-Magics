#pragma once

#include "PCH.h"
#include "Shared/HoveredMagicType.h"

namespace IntegratedMagic::HoveredForm {

    [[nodiscard]] RE::FormID GetHoveredFormID();
    [[nodiscard]] MagicType GetHoveredMagicType();

    void SetHoveredFormState(RE::FormID formID, MagicType type);
}
