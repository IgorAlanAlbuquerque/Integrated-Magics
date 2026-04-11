#pragma once
#include <xinput.h>

#include "Input/ExclusiveStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Input/SlotEdgeStore.h"

namespace Input::detail {

    void ReconcilePhysicalKeyState(KeyStateStore& keys, SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay,
                                   RetainedArr& retained, DeferredVec& deferred);

}