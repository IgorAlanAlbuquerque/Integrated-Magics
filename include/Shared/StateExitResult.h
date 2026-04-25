#pragma once

#include <optional>

#include "Shared/DispatchIntents.h"
#include "Shared/RestoreSnapshotPlan.h"

namespace IntegratedMagic {

    struct StateExitResult {
        std::optional<StopDispatchIntent> leftAttack;
        std::optional<StopDispatchIntent> rightAttack;
        std::optional<StopDispatchIntent> shout;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool waitForSheatheRestore{false};
        bool waitForPendingRestore{false};
        bool waitForPowerRestore{false};

        bool finalizeAfterController{false};
        bool resetShoutAfterController{false};
    };

}