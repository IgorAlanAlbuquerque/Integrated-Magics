#pragma once
#include "Domain/Hand.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAction {
    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Domain::Hand hand, bool skipAnim);
    void ClearHandSpell(RE::PlayerCharacter* player, Domain::Hand hand);
    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Domain::Hand hand);
    void EquipShoutInVoice(RE::PlayerCharacter* player, RE::TESForm* shoutOrPower);
    void ClearVoiceShout(RE::PlayerCharacter* player);
    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player, bool skipAnimOnReturn);
    void DisableSkipEquipVarsNow(RE::PlayerCharacter* player, bool skipAnim);
}