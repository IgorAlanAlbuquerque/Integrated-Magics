#pragma once
#include <vector>

#include "Domain/InventoryUtil.h"
#include "PCH.h"

namespace IntegratedMagic::Outbound {

    void RestoreOneHand(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                        bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot);

    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                                  std::vector<ExtraEquippedItem>& items);
}