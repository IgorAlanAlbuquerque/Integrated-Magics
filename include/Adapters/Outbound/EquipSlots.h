#pragma once

#ifdef GetObject
    #undef GetObject
#endif

#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::EquipUtil {
    inline const RE::BGSEquipSlot* GetHandEquipSlot(Hand hand) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }
        const auto id =
            (hand == Hand::Left) ? RE::DefaultObjectID::kLeftHandEquip : RE::DefaultObjectID::kRightHandEquip;
        auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
        return pp ? *pp : nullptr;
    }
}