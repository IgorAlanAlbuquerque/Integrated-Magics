#pragma once

#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand);

    bool TryAssignHoveredShoutToSlot(int slot);

    bool TryClearSlotHand(int slot, Slots::Hand hand);

    bool TryClearSlotShout(int slot);

}