#pragma once

#include <optional>
#include <vector>

#include "PCH.h"
#include "Shared/DispatchIntents.h"
#include "Shared/Hand.h"
#include "Shared/RestoreSnapshotPlan.h"
#include "Shared/SlotPressResult.h"

namespace IntegratedMagic {

    struct EquipIntent {
        RE::SpellItem* spell{nullptr};
        Hand hand{Hand::Right};
    };

    struct SlotPressAction {
        SlotPressResult result{SlotPressResult::None};

        std::vector<EquipIntent> spellsToEquip;
        RE::TESForm* shoutToEquip{nullptr};
        bool startShoutDispatch{false};
        bool skipAnim{false};

        std::optional<StopDispatchIntent> leftAttack;
        std::optional<StopDispatchIntent> rightAttack;
        std::optional<StopDispatchIntent> shout;

        std::optional<RestoreSnapshotPlan> restorePlan;
        bool finalizeAfterController{false};
        bool resetShoutAfterController{false};
        bool needsSkipEquipVars{false};
    };

}