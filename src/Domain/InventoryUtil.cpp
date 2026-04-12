#ifdef GetObject
    #undef GetObject
#endif

#include "InventoryUtil.h"

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

    float GetPlayerMagicka(RE::PlayerCharacter* player) {
        if (!player) return 0.0f;
        auto const* avo = player->AsActorValueOwner();
        return avo ? avo->GetActorValue(RE::ActorValue::kMagicka) : 0.0f;
    }

    float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem const* spell) {
        if (!player || !spell) return 0.0f;
        return spell->CalculateMagickaCost(player);
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

    bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem const* expected) {
        if (!caster || !expected) return false;
        auto const* cur = caster->currentSpell ? caster->currentSpell->As<RE::SpellItem>() : nullptr;
        if (!cur) return false;
        return cur != expected;
    }

    RE::ActorMagicCaster* GetMagicCaster(RE::PlayerCharacter* player, RE::MagicSystem::CastingSource source) {
        if (!player) return nullptr;
        auto* mc = player->GetMagicCaster(source);
        return mc ? skyrim_cast<RE::ActorMagicCaster*>(mc) : nullptr;
    }
}