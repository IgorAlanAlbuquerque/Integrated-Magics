#include "MagicState.h"

#include "MagicAction.h"
#include "MagicSlots.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace {
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

        RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject* base, RE::ExtraDataList* candidate) {
            if (!base || !candidate) {
                return nullptr;
            }

            auto* player = GetPlayer();
            if (!player) {
                return nullptr;
            }

            auto inv = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

            for (auto const& [obj, data] : inv) {
                if (obj != base) {
                    continue;
                }

                auto const* entry = data.second.get();
                if (!entry || !entry->extraLists) {
                    continue;
                }

                for (auto* extra : *entry->extraLists) {
                    if (extra == candidate) {
                        return extra;
                    }
                }
            }

            return nullptr;
        }

        RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject* base) {
            if (!base) {
                return nullptr;
            }

            auto* player = GetPlayer();
            if (!player) {
                return nullptr;
            }

            auto inv = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

            for (auto const& [obj, data] : inv) {
                if (obj != base) {
                    continue;
                }

                auto const* entry = data.second.get();
                if (!entry || !entry->extraLists) {
                    continue;
                }

                for (auto* extra : *entry->extraLists) {
                    if (extra) {
                        return extra;
                    }
                }
            }

            return nullptr;
        }

        const RE::BGSEquipSlot* GetHandEquipSlot(bool leftHand) {
            auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
            if (!dom) {
                return nullptr;
            }

            const auto id = leftHand ? RE::DefaultObjectID::kLeftHandEquip : RE::DefaultObjectID::kRightHandEquip;
            auto** pp = dom->GetObject<RE::BGSEquipSlot>(id);
            return pp ? *pp : nullptr;
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

        static bool IsWornNow(RE::TESBoundObject* base) {
            if (!base) return false;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;

            auto inv = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

            for (auto const& [obj, data] : inv) {
                if (obj != base) continue;

                auto const* entry = data.second.get();
                if (!entry || !entry->extraLists) continue;

                for (auto* extra : *entry->extraLists) {
                    if (!extra) continue;

                    if (extra->HasType(RE::ExtraDataType::kWorn) || extra->HasType(RE::ExtraDataType::kWornLeft)) {
                        return true;
                    }
                }
            }

            return false;
        }

        void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* mgr,
                                      std::vector<MagicState::ExtraEquippedItem>& items) {
            if (!actor || !mgr) {
                return;
            }

            for (auto const& it : items) {
                if (!it.base) {
                    continue;
                }

                if (IsWornNow(it.base)) {
                    continue;
                }

                RE::ExtraDataList* liveExtra = ResolveLiveExtra(it.base, it.extra);
                if (!liveExtra) {
                    liveExtra = FindAnyInstanceExtraForBase(it.base);
                }

                const bool isArmor = (it.base->GetFormType() == RE::FormType::Armor);

                const bool queue = false;
                const bool force = !isArmor;
                const bool applyNow = isArmor;

                mgr->EquipObject(actor, it.base,
                                 /*extra*/ liveExtra, 1, nullptr, queue, force, true, applyNow);
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

        if (auto const* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID); !spell) {
            return;
        }

        CaptureSnapshot(player);

        _prevExtraEquipped.clear();
        std::vector<ExtraEquippedItem> wornBefore;
        CaptureWornSnapshot(wornBefore);

        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
        if (!spell) return;

        const auto settings = SpellSettingsDB::Get().GetOrCreate(spellFormID);

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
        ReequipPrevExtraEquipped(player, mgr, _prevExtraEquipped);

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

        const auto* rightSlot = GetHandEquipSlot(false);
        const auto* leftSlot = GetHandEquipSlot(true);

        auto restoreOneHand = [&](bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot) {
            auto* curEntry = player->GetEquippedEntryData(leftHand);
            auto* curObj = curEntry ? curEntry->GetObject() : nullptr;
            auto* curBase = curObj ? curObj->As<RE::TESBoundObject>() : nullptr;
            auto* curExtra = GetPrimaryExtra(curEntry);

            if (want.base) {
                auto* desiredBase = want.base;

                RE::ExtraDataList* desiredExtra = ResolveLiveExtra(desiredBase, want.extra);
                if (!desiredExtra && want.extra) {
                    desiredExtra = FindAnyInstanceExtraForBase(desiredBase);
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
        std::vector<ExtraEquippedItem> before;
        CaptureWornSnapshot(before);

        equipFn();

        std::vector<ExtraEquippedItem> after;
        CaptureWornSnapshot(after);

        auto removed = DiffWornSnapshot(before, after);

        for (auto const& r : removed) {
            bool exists = false;
            for (auto const& e : _prevExtraEquipped) {
                if (e.base == r.base && e.extra == r.extra) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                _prevExtraEquipped.push_back(r);
            }
        }
    }
}
