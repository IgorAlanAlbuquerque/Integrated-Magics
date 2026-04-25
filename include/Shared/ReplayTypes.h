#pragma once

#include <optional>

#include "PCH.h"
#include "Shared/StateExitResult.h"

struct RetainedEvent {
    RE::INPUT_DEVICE dev{RE::INPUT_DEVICE::kKeyboard};
    std::uint32_t rawIdCode{0};
    RE::BSFixedString userEvent{};
    float value{0.f};
    float heldSecs{0.f};
};

struct DeferredReplayEvent {
    std::size_t slot{0};
    RetainedEvent ev{};
};

namespace Input::detail {

    struct DrainDeferredReplayResult {
        std::optional<RetainedEvent> replayEvent;
    };

    struct ProcessButtonEventsResult {
        std::optional<IntegratedMagic::StateExitResult> forceExit;
    };

}