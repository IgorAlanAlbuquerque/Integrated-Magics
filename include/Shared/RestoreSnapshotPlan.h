#pragma once

#include <vector>

#include "PCH.h"
#include "Shared/InventoryType.h"

namespace IntegratedMagic {

    struct RestoreSnapshotPlan {
        bool valid{false};

        bool applySkipEquipAnimReturn{false};
        bool skipEquipAnimReturn{false};

        InventoryIndex inventoryIndex{};

        bool restoreRightHand{false};
        bool restoreLeftHand{false};

        ObjSnapshot rightObj{};
        ObjSnapshot leftObj{};

        bool clearRightHand{false};
        RE::SpellItem* clearRightHandByRef{nullptr};

        bool clearLeftHand{false};
        RE::SpellItem* clearLeftHandByRef{nullptr};

        RE::SpellItem* equipRightSpell{nullptr};
        RE::SpellItem* equipLeftSpell{nullptr};

        bool restoreRightAfterLeftOnly{false};

        bool clearVoiceShout{false};
        RE::TESForm* equipVoiceForm{nullptr};

        std::vector<ExtraEquippedItem> prevExtraEquipped{};
    };

}