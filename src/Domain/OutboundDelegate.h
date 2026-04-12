#pragma once
#include <functional>
#include <vector>

#include "Domain/Hand.h"

namespace IntegratedMagic::Domain {

    struct OutboundDelegate {
        std::function<void(Hand, float, float)> dispatchAttack;
        std::function<void(float, float)> dispatchShout;

        std::function<void()> disableSkipEquipVarsNow;
        std::function<void()> applySkipEquipAnimReturn;

        std::function<void(RE::SpellItem*, Hand)> equipSpellInHand;
        std::function<void(Hand)> clearHandSpell;
        std::function<void(RE::SpellItem*, Hand)> clearHandSpellByRef;
        std::function<void(RE::TESForm*)> equipShoutInVoice;
        std::function<void()> clearVoiceShout;

        std::function<void(RE::PlayerCharacter*, std::vector<ExtraEquippedItem>&)> reequipPrevExtraEquipped;

        std::function<const RE::BGSEquipSlot*(Domain::Hand)> getHandEquipSlot;
        std::function<void(bool leftHand, const InventoryIndex&, const ObjSnapshot&, const RE::BGSEquipSlot*)>
            restoreOneHand;
    };

}