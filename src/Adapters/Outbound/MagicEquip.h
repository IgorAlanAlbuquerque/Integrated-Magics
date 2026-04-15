#pragma once
#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::MagicAction {
    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand, bool skipAnim);
    void ClearHandSpell(RE::PlayerCharacter* player, Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand);
    void EquipShoutInVoice(RE::PlayerCharacter* player, RE::TESForm* shoutOrPower);
    void ClearVoiceShout(RE::PlayerCharacter* player);
    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player, bool skipAnimOnReturn);
    void DisableSkipEquipVarsNow(RE::PlayerCharacter* player);
    void SetSkipEquipVars(RE::PlayerCharacter* pc, bool enable);
}