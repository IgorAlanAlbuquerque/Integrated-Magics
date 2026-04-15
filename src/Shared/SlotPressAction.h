#pragma once

#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/InventoryType.h"
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
        InventoryIndex inventorySnapshotBefore{};
    };
}