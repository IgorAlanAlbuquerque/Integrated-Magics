#include "Input/ReplaySystem.h"

#include "PCH.h"

namespace Input::detail {

    void ResetReplayState(std::size_t s, ReplayArr& replay) { replay[s] = ReplayState{}; }

    bool HasDeferredReplayForSlot(std::size_t s, const DeferredVec& deferred) {
        return std::ranges::any_of(deferred, [s](const DeferredReplayEvent& item) { return item.slot == s; });
    }

    void QueueDeferredReplayEvent(std::size_t s, const RetainedEvent& ev, DeferredVec& deferred) {
        deferred.emplace_back(s, ev);
    }

    void ClearDeferredReplayEventsForSlot(std::size_t s, DeferredVec& deferred) {
        std::erase_if(deferred, [s](const DeferredReplayEvent& item) { return item.slot == s; });
    }

    bool ReplayMatchesEvent(std::size_t s, RE::INPUT_DEVICE dev, std::uint32_t rawIdCode,
                            const RE::BSFixedString& userEvent, float value, const ReplayArr& replay) {
        const auto& rp = replay[s];
        if (!rp.armed) return false;
        if (rp.dev != dev) return false;
        if (rp.rawIdCode != rawIdCode) return false;
        if (rp.userEvent != userEvent) return false;
        return (value > 0.5f) == rp.valueAboveHalf;
    }

    DrainDeferredReplayResult DrainOneDeferredReplayEvent(ReplayArr& replay, DeferredVec& deferred) {
        DrainDeferredReplayResult result{};

        if (deferred.empty()) return result;

        const auto item = deferred.front();
        deferred.erase(deferred.begin());

        auto& rp = replay[item.slot];
        rp.armed = true;
        rp.dev = item.ev.dev;
        rp.rawIdCode = item.ev.rawIdCode;
        rp.userEvent = item.ev.userEvent;
        rp.valueAboveHalf = item.ev.value > 0.5f;

        result.replayEvent = item.ev;
        return result;
    }

}