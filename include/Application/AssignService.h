#pragma once

#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Hand hand);
    bool TryAssignHoveredShoutToSlot(int slot);
    bool TryClearSlotHand(int slot, Hand hand);
    bool TryClearSlotShout(int slot);

}