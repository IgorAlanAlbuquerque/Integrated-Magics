#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "Config/InputConstants.h"
#include "PCH.h"
#include "Shared/SlotPressAction.h"

struct ReplayState {
    bool armed{false};
    RE::INPUT_DEVICE dev{RE::INPUT_DEVICE::kKeyboard};
    std::uint32_t rawIdCode{0};
    RE::BSFixedString userEvent{};
    bool valueAboveHalf{false};
};

struct DeferredReplayEvent {
    std::size_t slot{0};
    IntegratedMagic::RetainedEvent ev{};
};

namespace Input::detail {

    using ReplayArr = std::array<ReplayState, kInputMaxSlots>;
    using RetainedArr = std::array<std::vector<IntegratedMagic::RetainedEvent>, kInputMaxSlots>;
    using DeferredVec = std::vector<DeferredReplayEvent>;

    void ResetReplayState(std::size_t s, ReplayArr& replay);

    [[nodiscard]] bool HasDeferredReplayForSlot(std::size_t s, const DeferredVec& deferred);

    void QueueDeferredReplayEvent(std::size_t s, const IntegratedMagic::RetainedEvent& ev, DeferredVec& deferred);

    void ClearDeferredReplayEventsForSlot(std::size_t s, DeferredVec& deferred);

    [[nodiscard]] bool ReplayMatchesEvent(std::size_t s, RE::INPUT_DEVICE dev, std::uint32_t rawIdCode,
                                          const RE::BSFixedString& userEvent, float value, const ReplayArr& replay);

    [[nodiscard]] IntegratedMagic::DrainDeferredReplayResult DrainOneDeferredReplayEvent(ReplayArr& replay,
                                                                                         DeferredVec& deferred);
}