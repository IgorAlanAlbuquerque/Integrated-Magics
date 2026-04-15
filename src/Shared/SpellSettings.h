#pragma once
#include "Shared/SpellType.h"

namespace IntegratedMagic {
    struct SpellSettings {
        ActivationMode mode{ActivationMode::Hold};
        bool autoAttack{true};
    };
}