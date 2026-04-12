#pragma once

#include "Domain/Hand.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Domain::Hand hand);
    bool TryAssignHoveredShoutToSlot(int slot);
    bool TryClearSlotHand(int slot, Domain::Hand hand);
    bool TryClearSlotShout(int slot);

}