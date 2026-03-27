#include "InventoryUtil.h"

#ifdef GetObject
    #undef GetObject
#endif

#include "Action.h"
#include "Config/EquipSlots.h"
#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic {

    InventoryIndex BuildInventoryIndex(RE::PlayerCharacter* player) {
        InventoryIndex idx{};
        if (!player) return idx;

        auto inv = player->GetInventory([](RE::TESBoundObject&) { return true; });
        for (auto const& [obj, data] : inv) {
            auto* base = obj;
            auto const* entry = data.second.get();
            if (!base || !entry || !entry->extraLists) continue;

            auto& vec = idx.extrasByBase[base];
            for (auto* extra : *entry->extraLists) {
                if (!extra) continue;
                vec.push_back(extra);
                if (extra->HasType(RE::ExtraDataType::kWorn) || extra->HasType(RE::ExtraDataType::kWornLeft)) {
                    idx.wornBases.insert(base);
                }
            }
        }
        return idx;
    }

    RE::ExtraDataList* GetWornExtraForHand(RE::InventoryEntryData const* entry, bool leftHand) {
        using enum RE::ExtraDataType;
        if (!entry || !entry->extraLists) return nullptr;

        const auto preferred = leftHand ? kWornLeft : kWorn;
        RE::ExtraDataList* firstNonNull = nullptr;
        RE::ExtraDataList* anyWorn = nullptr;

        for (auto* x : *entry->extraLists) {
            if (!x) continue;
            if (!firstNonNull) firstNonNull = x;
            if (x->HasType(preferred)) return x;
            if (!anyWorn && (x->HasType(kWorn) || x->HasType(kWornLeft))) anyWorn = x;
        }
        return anyWorn ? anyWorn : firstNonNull;
    }

    RE::ExtraDataList* ResolveLiveExtra(const InventoryIndex& idx, RE::TESBoundObject* base,
                                        RE::ExtraDataList const* candidate) {
        if (!base || !candidate) return nullptr;
        auto it = idx.extrasByBase.find(base);
        if (it == idx.extrasByBase.end()) return nullptr;
        for (auto* ex : it->second) {
            if (ex == candidate) return ex;
        }
        return nullptr;
    }

    RE::ExtraDataList* FindAnyInstanceExtraForBase(const InventoryIndex& idx, RE::TESBoundObject* base) {
        if (!base) return nullptr;
        auto it = idx.extrasByBase.find(base);
        if (it == idx.extrasByBase.end() || it->second.empty()) return nullptr;
        return it->second.front();
    }

    bool IsWornNow(const InventoryIndex& idx, RE::TESBoundObject* base) { return base && idx.wornBases.contains(base); }

    bool IsEquippedInHands(RE::Actor const* actor, RE::TESBoundObject const* base) {
        if (!actor || !base) return false;
        auto const* leftObj =
            actor->GetEquippedEntryData(true) ? actor->GetEquippedEntryData(true)->GetObject() : nullptr;
        auto const* rightObj =
            actor->GetEquippedEntryData(false) ? actor->GetEquippedEntryData(false)->GetObject() : nullptr;
        return (leftObj == base) || (rightObj == base);
    }

    void RestoreOneHand(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                        bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot) {
        auto* curEntry = player->GetEquippedEntryData(leftHand);
        auto* curBase = curEntry && curEntry->GetObject() ? curEntry->GetObject()->As<RE::TESBoundObject>() : nullptr;
        auto* curExtra = GetWornExtraForHand(curEntry, leftHand);

        if (want.base) {
            if (curBase == want.base) return;
            auto* desiredExtra = ResolveLiveExtra(idx, want.base, want.extra);
            if (!desiredExtra) desiredExtra = FindAnyInstanceExtraForBase(idx, want.base);
            mgr->EquipObject(player, want.base, desiredExtra, 1, slot, true, false, true, false);
            return;
        }
        if (!curBase) return;
        mgr->UnequipObject(player, curBase, curExtra, 1, slot, true, false, true, false, nullptr);
    }

    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                                  std::vector<ExtraEquippedItem>& items) {
        if (!actor || !mgr) return;
        for (auto const& it : items) {
            if (!it.base) continue;
            if (IsWornNow(idx, it.base)) continue;
            if (IsEquippedInHands(actor, it.base)) continue;
            if (it.base->GetFormType() == RE::FormType::Weapon) continue;

            auto* liveExtra = ResolveLiveExtra(idx, it.base, it.extra);
            if (!liveExtra) liveExtra = FindAnyInstanceExtraForBase(idx, it.base);

            const bool isArmor = (it.base->GetFormType() == RE::FormType::Armor);
            mgr->EquipObject(actor, it.base, liveExtra, 1, nullptr, true, false, true, isArmor);
        }
        items.clear();
    }

    float GetPlayerMagicka(RE::PlayerCharacter* player) {
        if (!player) return 0.0f;
        auto const* avo = player->AsActorValueOwner();
        return avo ? avo->GetActorValue(RE::ActorValue::kMagicka) : 0.0f;
    }

    float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem const* spell) {
        if (!player || !spell) return 0.0f;
        return spell->CalculateMagickaCost(player);
    }

    bool HasEnoughMagickaForSpell(RE::PlayerCharacter* player, RE::SpellItem const* spell) {
        const float cost = GetSpellMagickaCost(player, spell);
        if (cost <= 0.0f) return true;
        return (GetPlayerMagicka(player) + 1e-2f) >= cost;
    }

    float GetDualCastCostMultiplier(RE::PlayerCharacter const* player, RE::SpellItem const* spell) {
        if (!player || !spell) return 2.0f;

        RE::FormID dualCastPerkID = 0;
        switch (spell->GetAssociatedSkill()) {
            using enum RE::ActorValue;
            case kAlteration:
                dualCastPerkID = 0x000153CD;
                break;
            case kConjuration:
                dualCastPerkID = 0x000153CE;
                break;
            case kDestruction:
                dualCastPerkID = 0x000153CF;
                break;
            case kIllusion:
                dualCastPerkID = 0x000153D0;
                break;
            case kRestoration:
                dualCastPerkID = 0x000153D1;
                break;
            default:
                return 2.0f;
        }
        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(dualCastPerkID);
        return (perk && player->HasPerk(perk)) ? 2.8f : 2.0f;
    }

    bool IsChargeComplete(RE::ActorMagicCaster const* caster, RE::SpellItem const* spell) {
        if (!spell) return false;
        const float charge = spell->GetChargeTime();
        if (charge <= 0.0f) return true;
        if (!caster) return false;

        const auto st = caster->state.get();
        if (st == RE::MagicCaster::State::kReady) return true;
        if (st == RE::MagicCaster::State::kCharging) return (caster->castingTimer + 1e-3f) >= charge;
        return false;
    }

    bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem* expected) {
        if (!caster || !expected) return false;
        auto* cur = caster->currentSpell ? caster->currentSpell->As<RE::SpellItem>() : nullptr;
        if (!cur) return false;
        return cur != expected;
    }
}