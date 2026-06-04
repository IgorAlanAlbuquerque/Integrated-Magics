#pragma once

#include <optional>

#include "PCH.h"
#include "Shared/DispatchIntents.h"
#include "Shared/Hand.h"
#include "Shared/RestoreSnapshotPlan.h"

namespace IntegratedMagic {

    struct SpellFiredResult {
        struct StopHandDispatchIntent {
            Hand hand{Hand::Right};
            float heldSecs{-1.f};
        };

        std::optional<StopHandDispatchIntent> leftAttack;
        std::optional<StopHandDispatchIntent> rightAttack;
        bool finalizeLeft{false};
        bool finalizeRight{false};
    };

    struct PumpCastPhaseResult {
        bool startAttack{false};      // deferred DOWN: re-enable hold and dispatch DOWN
        bool releaseAttack{false};    // UP only: dispatch UP, DOWN deferred via pendingRestartNextFrame
        std::optional<StopDispatchIntent> stopAttack;
    };

    struct PumpResult {
        struct AttackEvent {
            Hand hand{Hand::Right};
            float power{0.f};
            float secsHeld{0.f};
        };

        struct ShoutEvent {
            float power{0.f};
            float secsHeld{0.f};
        };

        std::optional<AttackEvent> leftAttack;
        std::optional<AttackEvent> rightAttack;
        std::optional<ShoutEvent> shout;
    };

    using DisableHandResult = SingleHandStopResult;
    using PrepareOverwriteResult = DualHandStopResult;

    struct CastInterruptResult {
        float finishedLeft{-1.f};
        float finishedRight{-1.f};
        bool releaseLeft{false};   // UP only; DOWN deferred via pendingRestartNextFrame
        bool releaseRight{false};
    };

    struct PumpAutomaticResult {
        bool startLeftAttack{false};   // deferred DOWN (UP was sent last frame)
        bool startRightAttack{false};
        bool releaseLeftAttack{false};  // UP only (DOWN deferred to next frame)
        bool releaseRightAttack{false};

        std::optional<StopDispatchIntent> stopLeftAttack;
        std::optional<StopDispatchIntent> stopRightAttack;
        std::optional<StopDispatchIntent> stopShout;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool finalizeAfterExecution{false};
        bool resetShoutAfterExecution{false};
    };

}