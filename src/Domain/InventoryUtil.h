#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PCH.h"

namespace IntegratedMagic {

    struct ObjSnapshot {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
        RE::FormID formID{0};
    };

    struct InventoryIndex {
        std::unordered_map<RE::TESBoundObject*, std::vector<RE::ExtraDataList*>> extrasByBase;
        std::unordered_set<RE::TESBoundObject*> wornBases;
    };

    struct ExtraEquippedItem {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
    };

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