#pragma once

#include "InputState.h"

namespace Input::detail {

    [[nodiscard]] inline bool HasExclusivePending(std::size_t s) {
        return g_exclusivePendingSrc[s] != PendingSrc::None;
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

    void DiscardExclusivePending(std::size_t s);

    void ClearExclusivePending(std::size_t s, ClearReason reason);

    void ClearEdgeStateOnly();

    void ClearLikelyStuckKeysAfterMenuClose();

    void ResetExclusiveState();

    void RecomputeSlotEdges(float dt);

}