#pragma once
#include <optional>
#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/SlotMutation.h"

namespace IntegratedMagic::MagicAssign {

    [[nodiscard]] std::optional<SlotMutation> ComputeSpellAssignment(int slot, Hand hand,
                                                                      RE::FormID existingLeftID);

    [[nodiscard]] std::optional<SlotMutation> ComputeShoutAssignment(int slot);

    [[nodiscard]] SlotMutation ComputeClearHand(int slot, Hand hand);

    [[nodiscard]] SlotMutation ComputeClearShout(int slot);

}
