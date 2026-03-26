#pragma once

#include "InputState.h"

namespace Input::detail {

    void ResetReplayState(std::size_t s);

    [[nodiscard]] bool HasDeferredReplayForSlot(std::size_t s);

    void QueueDeferredReplayEvent(std::size_t s, const RetainedEvent& ev);

    void ClearDeferredReplayEventsForSlot(std::size_t s);

    [[nodiscard]] bool ReplayMatchesEvent(std::size_t s, RE::INPUT_DEVICE dev, std::uint32_t rawIdCode,
                                          const RE::BSFixedString& userEvent, float value);

    void DrainOneDeferredReplayEvent();

}