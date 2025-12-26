#pragma once

#include "MagicAction.h"
#include "PCH.h"

namespace IntegratedMagic::EquipUtil {
    inline const RE::BGSEquipSlot* GetHandEquipSlot(EquipHand hand) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }

        const auto id =
            (hand == EquipHand::Left) ? RE::DefaultObjectID::kLeftHandEquip : RE::DefaultObjectID::kRightHandEquip;

        auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
        return pp ? *pp : nullptr;
    }
}
