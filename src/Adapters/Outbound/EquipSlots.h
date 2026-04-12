#pragma once

#ifdef GetObject
    #undef GetObject
#endif

#include "Domain/Hand.h"
#include "PCH.h"

namespace IntegratedMagic::EquipUtil {
    inline const RE::BGSEquipSlot* GetHandEquipSlot(Domain::Hand hand) {
        auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
        if (!dom) {
            return nullptr;
        }
        const auto id =
            (hand == Domain::Hand::Left) ? RE::DefaultObjectID::kLeftHandEquip : RE::DefaultObjectID::kRightHandEquip;
        auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
        return pp ? *pp : nullptr;
    }
}