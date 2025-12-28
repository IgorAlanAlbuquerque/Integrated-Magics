#include "MagicState.h"

#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "MagicAction.h"
#include "MagicEquipSlots.h"
#include "MagicSlots.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace detail {
        namespace {
            constexpr std::uint32_t kRightAttackMouseId = 0;
            constexpr std::uint32_t kLeftAttackMouseId = 1;

            RE::BSFixedString& RightAttackEvent() {
                static RE::BSFixedString ev{"Right Attack/Block"};
                return ev;
            }
            RE::BSFixedString& LeftAttackEvent() {
                static RE::BSFixedString ev{"Left Attack/Block"};
                return ev;
            }

            struct SyntheticInputState {
                std::mutex mutex;
                std::queue<RE::ButtonEvent*> pending;
            };

            SyntheticInputState& GetSynth() {
                static SyntheticInputState s;  // NOSONAR
                return s;
            }

            RE::ButtonEvent* MakeAttackButtonEvent(bool leftHand, float value, float heldSecs) {
                const auto& ue = leftHand ? LeftAttackEvent() : RightAttackEvent();
                const auto id = leftHand ? kLeftAttackMouseId : kRightAttackMouseId;
                return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue, id, value, heldSecs);
            }
        }

        void EnqueueSyntheticAttack(RE::ButtonEvent* ev) {
            if (!ev) {
                return;
            }

            auto& st = GetSynth();
            std::scoped_lock lk(st.mutex);
            st.pending.push(ev);
        }

        void DispatchAttack(EquipHand hand, float value, float heldSecs) {
            using enum EquipHand;
            if (hand == Left || hand == Both) {
                EnqueueSyntheticAttack(MakeAttackButtonEvent(true, value, heldSecs));
            }
            if (hand == Right || hand == Both) {
                EnqueueSyntheticAttack(MakeAttackButtonEvent(false, value, heldSecs));
            }
        }

        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head) {
            auto& st = GetSynth();

            std::queue<RE::ButtonEvent*> local;
            {
                std::scoped_lock lk(st.mutex);
                local.swap(st.pending);
            }

            if (local.empty()) {
                return head;
            }

            RE::InputEvent* synthHead = nullptr;
            RE::InputEvent* synthTail = nullptr;

            while (!local.empty()) {
                auto* ev = local.front();
                local.pop();
                if (!ev) {
                    continue;
                }

                ev->next = nullptr;
                if (!synthHead) {
                    synthHead = ev;
                    synthTail = ev;
                } else {
                    synthTail->next = ev;
                    synthTail = ev;
                }
            }

            if (!head) {
                return synthHead;
            }
            synthTail->next = head;
            return synthHead;
        }
    }
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

        bool IsWornNow(const InventoryIndex& idx, RE::TESBoundObject* base) {
            return base && idx.wornBases.contains(base);
        }

        bool IsEquippedInHands(RE::Actor* actor, RE::TESBoundObject* base) {
            if (!actor || !base) {
                return false;
            }

            auto* leftEntry = actor->GetEquippedEntryData(true);
            auto* rightEntry = actor->GetEquippedEntryData(false);

            auto const* leftObj = leftEntry ? leftEntry->GetObject() : nullptr;
            auto const* rightObj = rightEntry ? rightEntry->GetObject() : nullptr;

            return leftObj == base || rightObj == base;
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

                if (IsEquippedInHands(actor, it.base)) {
                    continue;
                }

                RE::ExtraDataList* liveExtra = ResolveLiveExtra(idx, it.base, it.extra);
                if (!liveExtra) {
                    liveExtra = FindAnyInstanceExtraForBase(idx, it.base);
                }

                const bool isArmor = (it.base->GetFormType() == RE::FormType::Armor);

                mgr->EquipObject(actor, it.base, liveExtra, 1, nullptr, true, false, true, isArmor);
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
        if (_holdActive) {
            return;
        }

        if (slot < 0 || slot >= 4) {
            return;
        }

        if (!_active) {
            EnterPress(slot);
            return;
        }

        if (_activeSlot == slot) {
            ExitAll();
            return;
        }

        if (ApplyPress(slot)) {
            _activeSlot = slot;
        }
    }

    void MagicState::EnterPress(int slot) {
        auto* player = GetPlayer();
        if (!player) return;

        const auto spellFormID = MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) return;

        if (auto const* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID); !spell) return;

        CaptureSnapshot(player);

        _prevExtraEquipped.clear();

        ApplyPress(slot);

        _active = true;
        _activeSlot = slot;
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

                if (curBase == desiredBase) {
                    return;
                }

                mgr->EquipObject(player, desiredBase, desiredExtra, 1, slot, true, false, true, false);
            } else {
                if (!curBase) {
                    return;
                }

                mgr->UnequipObject(player, curBase, curExtra, 1, slot, true, false, true, false, nullptr);
            }
        };

        restoreOneHand(false, _snap.rightObj, rightSlot);
        restoreOneHand(true, _snap.leftObj, leftSlot);

        auto* leftSpell = _snap.leftSpell ? _snap.leftSpell->As<RE::SpellItem>() : nullptr;
        auto* rightSpell = _snap.rightSpell ? _snap.rightSpell->As<RE::SpellItem>() : nullptr;

        if (!leftSpell) {
            MagicAction::ClearHandSpell(player, EquipHand::Left);
        }
        if (!rightSpell) {
            MagicAction::ClearHandSpell(player, EquipHand::Right);
        }

        if (leftSpell) {
            MagicAction::EquipSpellInHand(player, leftSpell, EquipHand::Left);
        }
        if (rightSpell) {
            MagicAction::EquipSpellInHand(player, rightSpell, EquipHand::Right);
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

    void MagicState::HoldDown(int slot) {
        if (slot < 0 || slot >= 4) {
            return;
        }

        if (_holdActive) {
            return;
        }

        EnterHold(slot);
    }

    void MagicState::HoldUp(int slot) {
        if (!_holdActive) {
            return;
        }

        if (slot != _holdSlot) {
            return;
        }

        ExitAll();
    }

    void MagicState::EnterHold(int slot) {
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

        const auto ss = SpellSettingsDB::Get().GetOrCreate(spellFormID);

        if (!_active) {
            CaptureSnapshot(player);
            _prevExtraEquipped.clear();
            _active = true;
            _activeSlot = slot;
        }

        _holdActive = true;
        _holdSlot = slot;

        if (ss.autoAttack) {
            _waitingAutoAfterEquip = true;
            _waitingAutoHand = ss.hand;

            _attackEnabled = false;
        } else {
            StopAutoAttack();
            _waitingAutoAfterEquip = false;
        }

        UpdatePrevExtraEquippedForOverlay([&] { MagicAction::EquipSpellInHand(player, spell, ss.hand); });
    }

    void MagicState::ExitAll() {
        auto* player = GetPlayer();
        if (!player) {
            return;
        }

        StopAutoAttack();

        RestoreSnapshot(player);

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        auto idx = BuildInventoryIndex(player);
        if (mgr) {
            ReequipPrevExtraEquipped(player, mgr, idx, _prevExtraEquipped);
        }
        _prevExtraEquipped.clear();

        _holdActive = false;
        _holdSlot = -1;

        _active = false;
        _activeSlot = -1;

        _snap.valid = false;
    }

    void MagicState::StartAutoAttack(EquipHand hand) {
        _autoAttackHeld = true;
        _autoAttackHand = hand;
        _autoAttackSecs = 0.0f;

        IntegratedMagic::detail::DispatchAttack(hand, 1.0f, 0.0f);
    }

    void MagicState::StopAutoAttack() {
        if (!_autoAttackHeld) {
            return;
        }

        const float held = (_autoAttackSecs > 0.0f) ? _autoAttackSecs : 0.1f;

        IntegratedMagic::detail::DispatchAttack(_autoAttackHand, 0.0f, held);

        _autoAttackHeld = false;
        _autoAttackSecs = 0.0f;
    }

    void MagicState::PumpAutoAttack(float dt) {
        if (!_autoAttackHeld) {
            return;
        }

        _autoAttackSecs += (dt > 0.0f ? dt : 0.0f);

        IntegratedMagic::detail::DispatchAttack(_autoAttackHand, 1.0f, _autoAttackSecs);
    }

    void MagicState::RequestAutoAttackStart(EquipHand hand) {
        _waitingAutoAfterEquip = true;
        _waitingAutoHand = hand;

        if (_attackEnabled) {
            TryStartWaitingAutoAttack();
        }
    }

    void MagicState::NotifyAttackEnabled() {
        _attackEnabled = true;
        TryStartWaitingAutoAttack();
    }

    void MagicState::TryStartWaitingAutoAttack() {
        if (!_waitingAutoAfterEquip) {
            return;
        }
        if (!_holdActive) {
            return;
        }

        _waitingAutoAfterEquip = false;

        if (!_autoAttackHeld) {
            StartAutoAttack(_waitingAutoHand);
        }
    }
}
