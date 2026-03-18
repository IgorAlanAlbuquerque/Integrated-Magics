#pragma once
#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAction {
    RE::ActorMagicCaster* GetCaster(RE::PlayerCharacter* player, RE::MagicSystem::CastingSource source);
    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Slots::Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, Slots::Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Slots::Hand hand);
    void EquipShoutInVoice(RE::PlayerCharacter* player, RE::TESForm* shoutOrPower);
    void ClearVoiceShout(RE::PlayerCharacter* player);
    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player);
    void DisableSkipEquipVarsNow(RE::PlayerCharacter* player);
}