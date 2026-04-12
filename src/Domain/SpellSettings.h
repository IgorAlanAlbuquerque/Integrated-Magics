#pragma once
#include "Config/SpellType.h"

namespace IntegratedMagic {
    struct SpellSettings {
        ActivationMode mode{ActivationMode::Hold};
        bool autoAttack{true};
    };
}