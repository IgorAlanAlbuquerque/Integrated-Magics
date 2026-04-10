#pragma once
#include "Config/Slots.h"
#include "PCH.h"
namespace IntegratedMagic {
    struct SlotAffordability {
        float totalCost = 0.f;
        float available = 0.f;
        bool canCast = true;
        bool hasSpells = false;
    };

    SlotAffordability ComputeSlotAffordability(int slotIndex);
}