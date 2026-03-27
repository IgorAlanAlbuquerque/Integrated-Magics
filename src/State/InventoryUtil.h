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

    RE::ExtraDataList* GetWornExtraForHand(RE::InventoryEntryData const* entry, bool leftHand);

    RE::ExtraDataList* ResolveLiveExtra(const InventoryIndex& idx, RE::TESBoundObject* base,
                                        RE::ExtraDataList const* candidate);

    RE::ExtraDataList* FindAnyInstanceExtraForBase(const InventoryIndex& idx, RE::TESBoundObject* base);

    bool IsWornNow(const InventoryIndex& idx, RE::TESBoundObject* base);

    bool IsEquippedInHands(RE::Actor const* actor, RE::TESBoundObject const* base);

    void RestoreOneHand(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                        bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot);

    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                                  std::vector<ExtraEquippedItem>& items);

    float GetPlayerMagicka(RE::PlayerCharacter* player);
    float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem const* spell);
    bool HasEnoughMagickaForSpell(RE::PlayerCharacter* player, RE::SpellItem const* spell);
    float GetDualCastCostMultiplier(RE::PlayerCharacter const* player, RE::SpellItem const* spell);

    bool IsChargeComplete(RE::ActorMagicCaster const* caster, RE::SpellItem const* spell);
    bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem* expected);

}