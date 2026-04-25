#pragma once
#include <array>

#include "Config/InputConstants.h"

namespace Input {

    struct SlotHotkeys {
        std::array<int, 3> kb{-1, -1, -1};
        std::array<int, 3> gp{-1, -1, -1};
    };

    struct HotkeyCacheStore {
        std::array<SlotHotkeys, kInputMaxSlots> slots{};
        SlotHotkeys hud{};
    };

}