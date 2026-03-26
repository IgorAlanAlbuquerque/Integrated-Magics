#include "ReplaySystem.h"

#include <algorithm>

#include "PCH.h"

namespace Input::detail {

    void ResetReplayState(std::size_t s) { g_replay[s] = ReplayState{}; }

    bool HasDeferredReplayForSlot(std::size_t s) {
        return std::ranges::any_of(g_deferredEvents, [s](const DeferredReplayEvent& item) { return item.slot == s; });
    }

    void QueueDeferredReplayEvent(std::size_t s, const RetainedEvent& ev) {
        g_deferredEvents.push_back(DeferredReplayEvent{s, ev});
    }

    void ClearDeferredReplayEventsForSlot(std::size_t s) {
        g_deferredEvents.erase(std::remove_if(g_deferredEvents.begin(), g_deferredEvents.end(),
                                              [s](const DeferredReplayEvent& item) { return item.slot == s; }),
                               g_deferredEvents.end());
    }

    bool ReplayMatchesEvent(std::size_t s, RE::INPUT_DEVICE dev, std::uint32_t rawIdCode,
                            const RE::BSFixedString& userEvent, float value) {
        const auto& rp = g_replay[s];
        if (!rp.armed) return false;
        if (rp.dev != dev) return false;
        if (rp.rawIdCode != rawIdCode) return false;
        if (rp.userEvent != userEvent) return false;
        return (value > 0.5f) == rp.valueAboveHalf;
    }

    void DrainOneDeferredReplayEvent() {
        if (g_deferredEvents.empty()) return;

        const auto item = g_deferredEvents.front();
        g_deferredEvents.erase(g_deferredEvents.begin());

        auto& rp = g_replay[item.slot];
        rp.armed = true;
        rp.dev = item.ev.dev;
        rp.rawIdCode = item.ev.rawIdCode;
        rp.userEvent = item.ev.userEvent;
        rp.valueAboveHalf = item.ev.value > 0.5f;
        rp.skipNextSimWindowOpen = rp.valueAboveHalf;

#ifdef DEBUG
        spdlog::info("[Input] Replay: slot={} dequeue dev={} value={:.2f} heldSecs={:.3f}", item.slot,
                     static_cast<int>(item.ev.dev), item.ev.value, item.ev.heldSecs);
#endif

        IntegratedMagic::detail::EnqueueRetainedEvent(item.ev.dev, item.ev.rawIdCode, item.ev.userEvent, item.ev.value,
                                                      item.ev.heldSecs);
    }

}