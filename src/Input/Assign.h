#pragma once
#include <cstdint>

#include "Config/Slots.h"

namespace IntegratedMagic::MagicAssign {

    enum class HoveredMagicType { None, Spell, TwoHandedSpell, Shout, Power };
    bool IsTwoHandedSpell(const RE::SpellItem* spell);

    HoveredMagicType GetHoveredMagicType();
    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand);
    bool TryAssignHoveredShoutToSlot(int slot);
    bool TryClearSlotHand(int slot, Slots::Hand hand);
    bool TryClearSlotShout(int slot);
    void RegisterEquipListener();
    void ClearLastEquippedMagic();
}