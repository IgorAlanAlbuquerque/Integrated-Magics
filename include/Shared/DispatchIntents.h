#pragma once

#include <optional>

namespace IntegratedMagic {

    struct StopDispatchIntent {
        float heldSecs{0.f};
    };

    struct SingleHandStopResult {
        std::optional<StopDispatchIntent> attack;
    };

    struct DualHandStopResult {
        std::optional<StopDispatchIntent> leftAttack;
        std::optional<StopDispatchIntent> rightAttack;
    };

    struct StartDualAttackResult {
        bool startLeftAttack{false};
        bool startRightAttack{false};
    };

}