#pragma once

#include "PCH.h"
#include "Shared/InventoryType.h"

namespace IntegratedMagic {

    InventoryIndex BuildInventoryIndex(RE::PlayerCharacter* player);

    [[nodiscard]] RE::ExtraDataList* GetWornExtraForHand(RE::InventoryEntryData const* entry, bool leftHand);

    [[nodiscard]] float GetPlayerMagicka(RE::PlayerCharacter* player);
    [[nodiscard]] float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem const* spell);
    [[nodiscard]] float GetDualCastCostMultiplier(RE::PlayerCharacter const* player, RE::SpellItem const* spell);

    [[nodiscard]] bool IsChargeComplete(RE::ActorMagicCaster const* caster, RE::SpellItem const* spell);
    [[nodiscard]] bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem const* expected);

    [[nodiscard]] RE::ActorMagicCaster* GetMagicCaster(RE::PlayerCharacter* player,
                                                       RE::MagicSystem::CastingSource source);
}