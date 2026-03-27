#pragma once

#include <optional>

#include "InputState.h"

namespace Input {

    [[nodiscard]] std::optional<int> ConsumePressedSlot();

    [[nodiscard]] std::optional<int> ConsumeReleasedSlot();

}