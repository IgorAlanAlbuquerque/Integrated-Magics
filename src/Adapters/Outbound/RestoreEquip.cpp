#ifdef GetObject
    #undef GetObject
#endif

#include "Adapters/Outbound/RestoreEquip.h"

#include "Shared/InventoryUtil.h"
#include "PCH.h"

namespace IntegratedMagic::Outbound {

    bool IsWornNow(const InventoryIndex& idx, RE::TESBoundObject* base) { return base && idx.wornBases.contains(base); }

    bool IsEquippedInHands(RE::Actor const* actor, RE::TESBoundObject const* base) {
        if (!actor || !base) return false;
        auto const* leftObj =
            actor->GetEquippedEntryData(true) ? actor->GetEquippedEntryData(true)->GetObject() : nullptr;
        auto const* rightObj =
            actor->GetEquippedEntryData(false) ? actor->GetEquippedEntryData(false)->GetObject() : nullptr;
        return (leftObj == base) || (rightObj == base);
    }

    RE::ExtraDataList* FindAnyInstanceExtraForBase(const InventoryIndex& idx, RE::TESBoundObject* base) {
        if (!base) return nullptr;
        auto it = idx.extrasByBase.find(base);
        if (it == idx.extrasByBase.end() || it->second.empty()) return nullptr;
        return it->second.front();
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
}