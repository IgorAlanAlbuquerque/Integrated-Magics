#include "MagicState.h"

#include <unordered_set>

#include "MagicAction.h"
#include "MagicEquipSlots.h"
#include "MagicSlots.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace {
        struct InventoryIndex {
            std::unordered_map<RE::TESBoundObject*, std::vector<RE::ExtraDataList*>> extrasByBase;
            std::unordered_set<RE::TESBoundObject*> wornBases;
        };

        InventoryIndex BuildInventoryIndex(RE::PlayerCharacter* player) {
            InventoryIndex idx;
            if (!player) return idx;

            auto inv = player->GetInventory([](RE::TESBoundObject&) { return true; });
            for (auto const& [obj, data] : inv) {
                auto* base = obj;
                auto const* entry = data.second.get();
                if (!base || !entry || !entry->extraLists) {
                    continue;
                }

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

        inline RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }

        inline RE::TESBoundObject* AsBoundObject(RE::TESForm* f) { return f ? f->As<RE::TESBoundObject>() : nullptr; }

        RE::ExtraDataList* GetPrimaryExtra(RE::InventoryEntryData* entry) {
            if (!entry || !entry->extraLists) {
                return nullptr;
            }
            for (auto* x : *entry->extraLists) {
                if (x) {
                    return x;
                }
            }
            return nullptr;
        }

        RE::ExtraDataList* ResolveLiveExtra(const InventoryIndex& idx, RE::TESBoundObject* base,
                                            RE::ExtraDataList* candidate) {
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

        void CaptureWornSnapshot(std::vector<MagicState::ExtraEquippedItem>& out) {
            out.clear();

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }

            auto inv = player->GetInventory([](RE::TESBoundObject&) { return true; });

            for (auto const& [obj, data] : inv) {
                auto const* entry = data.second.get();
                if (!obj || !entry || !entry->extraLists) {
                    continue;
                }

                for (auto* extra : *entry->extraLists) {
                    if (!extra) {
                        continue;
                    }

                    const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
                    const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);

                    if (worn || wornLeft) {
                        out.push_back({obj, extra});
                    }
                }
            }
        }

        std::vector<MagicState::ExtraEquippedItem> DiffWornSnapshot(
            const std::vector<MagicState::ExtraEquippedItem>& before,
            const std::vector<MagicState::ExtraEquippedItem>& after) {
            std::vector<MagicState::ExtraEquippedItem> removed;

            for (auto const& b : before) {
                bool stillWorn = false;
                for (auto const& a : after) {
                    if (a.base == b.base && a.extra == b.extra) {
                        stillWorn = true;
                        break;
                    }
                }
                if (!stillWorn) {
                    removed.push_back(b);
                }
            }
            return removed;
        }

        bool IsWornNow(const InventoryIndex& idx, RE::TESBoundObject* base) {
            return base && idx.wornBases.contains(base);
        }

        void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* mgr, const InventoryIndex& idx,
                                      std::vector<MagicState::ExtraEquippedItem>& items) {
            if (!actor || !mgr) {
                return;
            }

            for (auto const& it : items) {
                if (!it.base) {
                    continue;
                }

                if (IsWornNow(idx, it.base)) {
                    continue;
                }

                RE::ExtraDataList* liveExtra = ResolveLiveExtra(idx, it.base, it.extra);
                if (!liveExtra) {
                    liveExtra = FindAnyInstanceExtraForBase(idx, it.base);
                }

                const bool isArmor = (it.base->GetFormType() == RE::FormType::Armor);

                const bool queue = false;
                const bool force = !isArmor;
                const bool applyNow = isArmor;

                mgr->EquipObject(actor, it.base, liveExtra, 1, nullptr, queue, force, true, applyNow);
            }

            items.clear();
        }

    }

    MagicState& MagicState::Get() {
        static MagicState inst;  // NOSONAR
        return inst;
    }

    bool MagicState::IsActive() const { return _active; }

    int MagicState::ActiveSlot() const { return _activeSlot; }

    void MagicState::TogglePress(int slot) {
        if (slot < 0 || slot >= 4) {
            return;
        }

        if (!_active) {
            EnterPress(slot);
            return;
        }

        if (_activeSlot == slot) {
            ExitPress();
            return;
        }

        if (ApplyPress(slot)) {
            _activeSlot = slot;
        }
    }

    void MagicState::EnterPress(int slot) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        const std::uint32_t spellFormID = MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) {
            return;
        }
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
        if (!spell) {
            return;
        }

        CaptureSnapshot(player);

        _prevExtraEquipped.clear();
        std::vector<ExtraEquippedItem> wornBefore;
        CaptureWornSnapshot(wornBefore);

        const auto settings = SpellSettingsDB::Get().GetOrCreate(spellFormID);
        UpdatePrevExtraEquippedForOverlay([&] { MagicAction::EquipSpellInHand(player, spell, settings.hand); });

        std::vector<ExtraEquippedItem> wornAfter;
        CaptureWornSnapshot(wornAfter);

        _prevExtraEquipped = DiffWornSnapshot(wornBefore, wornAfter);

        _active = true;
        _activeSlot = slot;
    }

    void MagicState::ExitPress() {
        auto* player = GetPlayer();
        if (!player) {
            _active = false;
            _activeSlot = -1;
            _snap = {};
            return;
        }

        RestoreSnapshot(player);

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        auto idx = BuildInventoryIndex(GetPlayer());
        ReequipPrevExtraEquipped(player, mgr, idx, _prevExtraEquipped);

        _active = false;
        _activeSlot = -1;
        _snap = {};
    }

    void MagicState::CaptureSnapshot(RE::PlayerCharacter* player) {
        _snap = {};
        _snap.valid = false;

        if (!player) {
            return;
        }

        if (auto* r = player->GetEquippedEntryData(false)) {
            if (auto* obj = r->GetObject()) {
                if (auto* base = obj->As<RE::TESBoundObject>()) {
                    _snap.rightObj.base = base;
                    _snap.rightObj.extra = GetPrimaryExtra(r);
                    _snap.rightObj.formID = obj->GetFormID();
                }
            }
        }

        if (auto* l = player->GetEquippedEntryData(true)) {
            if (auto* obj = l->GetObject()) {
                if (auto* base = obj->As<RE::TESBoundObject>()) {
                    _snap.leftObj.base = base;
                    _snap.leftObj.extra = GetPrimaryExtra(l);
                    _snap.leftObj.formID = obj->GetFormID();
                }
            }
        }

        if (auto* rc = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kRightHand)) {
            _snap.rightSpell = rc->currentSpell;
        }
        if (auto* lc = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand)) {
            _snap.leftSpell = lc->currentSpell;
        }

        _snap.valid = true;
    }

    void MagicState::RestoreSnapshot(RE::PlayerCharacter* player) {
        if (!player || !_snap.valid) {
            return;
        }

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            return;
        }

        const auto idx = BuildInventoryIndex(player);

        const auto* rightSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(EquipHand::Right);
        const auto* leftSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(EquipHand::Left);

        auto restoreOneHand = [&](bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot) {
            auto* curEntry = player->GetEquippedEntryData(leftHand);
            auto* curObj = curEntry ? curEntry->GetObject() : nullptr;
            auto* curBase = curObj ? curObj->As<RE::TESBoundObject>() : nullptr;
            auto* curExtra = GetPrimaryExtra(curEntry);

            if (want.base) {
                auto* desiredBase = want.base;

                RE::ExtraDataList* desiredExtra = ResolveLiveExtra(idx, desiredBase, want.extra);
                if (!desiredExtra && want.extra) {
                    desiredExtra = FindAnyInstanceExtraForBase(idx, desiredBase);
                }

                if (curBase == desiredBase && (!desiredExtra || desiredExtra == curExtra)) {
                    return;
                }

                mgr->EquipObject(player, desiredBase, desiredExtra, 1, slot, true, true, true, false);
            } else {
                if (!curBase) {
                    return;
                }

                mgr->UnequipObject(player, curBase, curExtra, 1, slot, true, true, true, false, nullptr);
            }
        };

        restoreOneHand(false, _snap.rightObj, rightSlot);
        restoreOneHand(true, _snap.leftObj, leftSlot);

        auto* leftSpell = _snap.leftSpell ? _snap.leftSpell->As<RE::SpellItem>() : nullptr;
        auto* rightSpell = _snap.rightSpell ? _snap.rightSpell->As<RE::SpellItem>() : nullptr;

        if (leftSpell) {
            MagicAction::EquipSpellInHand(player, leftSpell, EquipHand::Left);
        } else {
            MagicAction::ClearHandSpell(player, EquipHand::Left);
        }

        if (rightSpell) {
            MagicAction::EquipSpellInHand(player, rightSpell, EquipHand::Right);
        } else {
            MagicAction::ClearHandSpell(player, EquipHand::Right);
        }
    }

    bool MagicState::ApplyPress(int slot) {
        auto* player = GetPlayer();
        if (!player) return false;

        const auto spellFormID = MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) return false;

        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
        if (!spell) return false;

        const auto settings = SpellSettingsDB::Get().GetOrCreate(spellFormID);

        UpdatePrevExtraEquippedForOverlay([&] { MagicAction::EquipSpellInHand(player, spell, settings.hand); });

        return true;
    }

    void MagicState::UpdatePrevExtraEquippedForOverlay(const std::function<void()>& equipFn) {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        auto before = BuildInventoryIndex(player);

        equipFn();

        auto after = BuildInventoryIndex(player);

        for (auto* base : before.wornBases) {
            if (after.wornBases.contains(base)) {
                continue;
            }

            ExtraEquippedItem item{base, nullptr};

            bool exists = false;
            for (auto const& e : _prevExtraEquipped) {
                if (e.base == item.base) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                _prevExtraEquipped.push_back(item);
            }
        }
    }
}
