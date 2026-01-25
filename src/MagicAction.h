#pragma once
#include "MagicSlots.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAction {
    RE::ActorMagicCaster* GetCaster(RE::PlayerCharacter* player, RE::MagicSystem::CastingSource source);

    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, MagicSlots::Hand hand);

    void ClearHandSpell(RE::PlayerCharacter* player, MagicSlots::Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, MagicSlots::Hand hand);

    void EquipSlotSpells(RE::PlayerCharacter* player, int slot);
}