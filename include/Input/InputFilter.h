#pragma once
#include "Input/CaptureState.h"
#include "Input/ExclusiveStore.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Shared/ReplayTypes.h"
#include "Input/SlotEdgeStore.h"
#include "PCH.h"

namespace Input::detail {

    [[nodiscard]] bool IsInputBlockedByMenus();
    [[nodiscard]] ProcessButtonEventsResult ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap,
                                                                bool& wantCapture, KeyStateStore& keys);
    void FilterMouseForPopup(RE::InputEvent** a_evns);
    void FilterEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                      const HotkeyCacheStore& cache, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained);
    void UpdateSlotsIfAllowed(bool blocked, float dt, SlotEdgeStore& slots, ExclusiveStore& excl,
                              const HotkeyCacheStore& cache, const KeyStateStore& keys, RetainedArr& retained,
                              DeferredVec& deferred, ReplayArr& replay, bool spellSystemActive, int activeSlot);
}