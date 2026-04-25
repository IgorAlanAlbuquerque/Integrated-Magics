#pragma once
#include "Config/InputConstants.h"
#include "Config/Ports/PatchSettings.h"
#include "Input/ExclusiveStore.h"
#include "Input/HotkeyCacheStore.h"
#include "Input/KeyStateStore.h"
#include "Input/ReplaySystem.h"
#include "Input/SlotEdgeStore.h"

namespace Input::detail {

    [[nodiscard]] inline bool HasExclusivePending(std::size_t s, const ExclusiveStore& excl) {
        return excl.pendingSrc[s] != Input::PendingSrc::None;
    }

    [[nodiscard]] inline bool IsAllowedExtra_Keyboard_MoveOrCamera(int code) {
        switch (code) {
            case kDIK_W:
            case kDIK_A:
            case kDIK_S:
            case kDIK_D:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] inline bool IsAllowedExtra_Gamepad_MoveOrCamera(int) { return false; }

    void DiscardExclusivePending(std::size_t s, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                                 DeferredVec& deferred);

    void ClearExclusivePending(std::size_t s, ClearReason reason, ExclusiveStore& excl, ReplayArr& replay,
                               RetainedArr& retained, DeferredVec& deferred);

    void ClearEdgeStateOnly(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                            DeferredVec& deferred);

    void ClearLikelyStuckKeysAfterMenuClose(KeyStateStore& keys, SlotEdgeStore& slots, ExclusiveStore& excl,
                                            const HotkeyCacheStore& cache, ReplayArr& replay, RetainedArr& retained,
                                            DeferredVec& deferred);

    void ResetExclusiveState(SlotEdgeStore& slots, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained,
                             DeferredVec& deferred);

    void RecomputeSlotEdges(float dt, SlotEdgeStore& slots, ExclusiveStore& excl, const HotkeyCacheStore& cache,
                            const KeyStateStore& keys, const IntegratedMagic::Config::IPatchSettings& patches,
                            ReplayArr& replay, RetainedArr& retained, DeferredVec& deferred, bool spellSystemActive,
                            int activeSlot);

    void TickFilterWindows(float dt, ExclusiveStore& excl, SlotEdgeStore& slots, RetainedArr& retained,
                           DeferredVec& deferred);
}