#pragma once

#include "MagicSlots.h"
#include "PCH.h"

namespace IntegratedMagic::EquipUtil {
    inline const RE::BGSEquipSlot* GetHandEquipSlot(IntegratedMagic::MagicSlots::Hand hand) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }

        const auto id = (hand == MagicSlots::Hand::Left) ? RE::DefaultObjectID::kLeftHandEquip
                                                         : RE::DefaultObjectID::kRightHandEquip;

        auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
        return pp ? *pp : nullptr;
    }
}