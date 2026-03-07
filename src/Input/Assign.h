#pragma once
#include <cstdint>

#include "Config/Slots.h"

namespace IntegratedMagic::MagicAssign {
    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand);
    bool TryClearSlotHand(int slot, Slots::Hand hand);
}