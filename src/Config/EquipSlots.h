#pragma once

#include "PCH.h"
#include "Slots.h"

namespace IntegratedMagic::EquipUtil {
    inline const RE::BGSEquipSlot* GetHandEquipSlot(IntegratedMagic::Slots::Hand hand) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }
        const auto id =
            (hand == Slots::Hand::Left) ? RE::DefaultObjectID::kLeftHandEquip : RE::DefaultObjectID::kRightHandEquip;
        auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
        return pp ? *pp : nullptr;
    }
}