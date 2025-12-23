#pragma once
#include "MagicSelect.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic::MagicAction {
    RE::ActorMagicCaster* GetCaster(RE::PlayerCharacter* player, RE::MagicSystem::CastingSource source);
    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, EquipHand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, EquipHand hand);
}
