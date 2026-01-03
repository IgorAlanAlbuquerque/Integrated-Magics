#include "MagicState.h"

#include <vector>

#include "MagicAction.h"
#include "MagicEquipSlots.h"
#include "MagicSlots.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace detail {
        SyntheticInputState& GetSynth() {
            static SyntheticInputState s;  // NOSONAR
            return s;
        }

        RE::ButtonEvent* MakeAttackButtonEvent(bool leftHand, float value, float heldSecs) {
            const auto& ue = leftHand ? LeftAttackEvent() : RightAttackEvent();
            const auto id = leftHand ? kLeftAttackMouseId : kRightAttackMouseId;
            return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue, id, value, heldSecs);
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
        inline RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }

        RE::ExtraDataList* GetWornExtraForHand(RE::InventoryEntryData* entry, bool leftHand) {
            if (!entry || !entry->extraLists) {
                return nullptr;
            }

            const auto wantType = leftHand ? RE::ExtraDataType::kWornLeft : RE::ExtraDataType::kWorn;

            for (auto* x : *entry->extraLists) {
                if (x && x->HasType(wantType)) {
                    return x;
                }
            }

            for (auto* x : *entry->extraLists) {
                if (x && (x->HasType(RE::ExtraDataType::kWorn) || x->HasType(RE::ExtraDataType::kWornLeft))) {
                    return x;
                }
            }

            for (auto* x : *entry->extraLists) {
                if (x) return x;
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

        float GetPlayerMagicka(RE::PlayerCharacter* player) {
            if (!player) {
                return 0.0f;
            }

            if (auto const* avo = player->AsActorValueOwner(); avo) {
                return avo->GetActorValue(RE::ActorValue::kMagicka);
            }

            return 0.0f;
        }

        float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem* spell, IntegratedMagic::EquipHand hand) {
            if (!player || !spell) {
                return 0.0f;
            }

            float cost = spell->CalculateMagickaCost(player);

            if (hand == EquipHand::Both) {
                cost *= 2.8f;
            }

            return cost;
        }

        bool HasEnoughMagickaForSpell(RE::PlayerCharacter* player, RE::SpellItem* spell,
                                      IntegratedMagic::EquipHand hand) {
            const float mag = GetPlayerMagicka(player);
            const float cost = GetSpellMagickaCost(player, spell, hand);

            if (cost <= 0.0f) {
                return true;
            }
            const bool ok = (mag + 1e-2f) >= cost;
            return ok;
        }

        inline RE::SpellItem* AsSpell(RE::TESForm* f) { return f ? f->As<RE::SpellItem>() : nullptr; }

        inline void ClearHandSpellIfNoSnapshot(RE::PlayerCharacter* player, RE::SpellItem* snapSpell,
                                               RE::SpellItem* modeSpell, EquipHand hand) {
            if (snapSpell) {
                return;
            }

            if (modeSpell) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, modeSpell, hand);
            } else {
                IntegratedMagic::MagicAction::ClearHandSpell(player, hand);
            }
        }

        inline void EquipSpellIfPresent(RE::PlayerCharacter* player, RE::SpellItem* spell, EquipHand hand) {
            if (spell) {
                IntegratedMagic::MagicAction::EquipSpellInHand(player, spell, hand);
            }
        }

        using InventoryIndexT = decltype(BuildInventoryIndex(std::declval<RE::PlayerCharacter*>()));

        inline void RestoreOneHand(RE::PlayerCharacter* player, RE::ActorEquipManager* mgr, const InventoryIndexT& idx,
                                   bool leftHand, const ObjSnapshot& want, const RE::BGSEquipSlot* slot) {
            auto* curEntry = player->GetEquippedEntryData(leftHand);
            auto* curObj = curEntry ? curEntry->GetObject() : nullptr;
            auto* curBase = curObj ? curObj->As<RE::TESBoundObject>() : nullptr;
            auto* curExtra = GetWornExtraForHand(curEntry, leftHand);

            if (want.base) {
                if (curBase == want.base) {
                    return;
                }

                auto* desiredExtra = ResolveLiveExtra(idx, want.base, want.extra);

                if (!desiredExtra) {
                    desiredExtra = FindAnyInstanceExtraForBase(idx, want.base);
                }

                const bool queue = (desiredExtra == nullptr);

                mgr->EquipObject(player, want.base, desiredExtra, 1, slot,
                                 /*queue*/ queue,
                                 /*force*/ false,
                                 /*sounds*/ true,
                                 /*applyNow*/ false);

                return;
            }

            if (!curBase) {
                return;
            }

            mgr->UnequipObject(player, curBase, curExtra, 1, slot,
                               /*queue*/ true,
                               /*force*/ false,
                               /*sounds*/ true,
                               /*applyNow*/ false, nullptr);
        }
    }

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

    MagicState& MagicState::Get() {
        static MagicState inst;  // NOSONAR
        return inst;
    }

    void MagicState::TogglePress(int slot) {
        if (_holdActive || _autoActive) {
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

    void MagicState::EnterPress(int slot) { (void)ApplyPress(slot); }

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
                    _snap.rightObj.extra = GetWornExtraForHand(r, false);
                    _snap.rightObj.formID = obj->GetFormID();
                }
            }
        }

        if (auto* l = player->GetEquippedEntryData(true)) {
            if (auto* obj = l->GetObject()) {
                if (auto* base = obj->As<RE::TESBoundObject>()) {
                    _snap.leftObj.base = base;
                    _snap.leftObj.extra = GetWornExtraForHand(l, true);
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
        using enum EquipHand;
        if (!player || !_snap.valid) {
            return;
        }

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            return;
        }

        const auto idx = BuildInventoryIndex(player);

        const auto* rightSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(Right);
        const auto* leftSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(Left);

        auto* leftSpell = AsSpell(_snap.leftSpell);
        auto* rightSpell = AsSpell(_snap.rightSpell);

        ClearHandSpellIfNoSnapshot(player, leftSpell, _modeSpellLeft, Left);
        ClearHandSpellIfNoSnapshot(player, rightSpell, _modeSpellRight, Right);

        RestoreOneHand(player, mgr, idx, false, _snap.rightObj, rightSlot);
        RestoreOneHand(player, mgr, idx, true, _snap.leftObj, leftSlot);

        EquipSpellIfPresent(player, leftSpell, Left);
        EquipSpellIfPresent(player, rightSpell, Right);
    }

    bool MagicState::ApplyPress(int slot) {
        RE::PlayerCharacter* player = nullptr;
        RE::SpellItem* spell = nullptr;
        SpellSettings ss{};

        if (!PrepareSlotSpell(slot, false, player, spell, ss)) {
            return false;
        }

        const auto hand = ss.hand;
        UpdatePrevExtraEquippedForOverlay(
            [player, spell, hand] { MagicAction::EquipSpellInHand(player, spell, hand); });

        return true;
    }

    void MagicState::HoldDown(int slot) {
        if (slot < 0 || slot >= 4) {
            return;
        }

        if (_holdActive || _autoActive) {
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
        RE::PlayerCharacter* player = nullptr;
        RE::SpellItem* spell = nullptr;
        SpellSettings ss{};

        if (!PrepareSlotSpell(slot, false, player, spell, ss)) {
            return;
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

        const auto hand = ss.hand;
        UpdatePrevExtraEquippedForOverlay(
            [player, spell, hand] { MagicAction::EquipSpellInHand(player, spell, hand); });
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

        _holdActive = false;
        _holdSlot = -1;

        _autoActive = false;
        _autoSlot = -1;
        _autoHand = EquipHand::Right;
        _autoWaitingChargeComplete = false;

        _active = false;
        _activeSlot = -1;

        _waitingAutoAfterEquip = false;
        _waitingAutoHand = EquipHand::Right;
        _attackEnabled = false;

        _autoAttackHeld = false;
        _autoAttackHand = EquipHand::Right;
        _autoAttackSecs = 0.0f;
        _autoChargeComplete = false;
        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;

        _snap.valid = false;
    }

    void MagicState::AutoExit() {
        if (_autoChargeComplete) {
            ExitAll();
        }
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
        if (!(_holdActive || _autoActive)) return;
        _attackEnabled = true;
        TryStartWaitingAutoAttack();
    }

    void MagicState::TryStartWaitingAutoAttack() {
        if (!_waitingAutoAfterEquip) {
            return;
        }
        if (!(_holdActive || _autoActive)) {
            return;
        }

        _waitingAutoAfterEquip = false;
        if (!_autoAttackHeld) {
            StartAutoAttack(_waitingAutoHand);
        }
    }

    void MagicState::EnterAutomatic(int slot) {
        if (slot < 0 || slot >= 4) {
            return;
        }

        if (_holdActive || _autoActive) {
            return;
        }

        RE::PlayerCharacter* player = nullptr;
        RE::SpellItem* spell = nullptr;
        SpellSettings ss{};

        if (!PrepareSlotSpell(slot, true, player, spell, ss)) {
            return;
        }

        _autoActive = true;
        _autoSlot = slot;
        _autoHand = ss.hand;
        _autoWaitingChargeComplete = true;

        _attackEnabled = false;
        RequestAutoAttackStart(ss.hand);

        const auto hand = ss.hand;
        UpdatePrevExtraEquippedForOverlay(
            [player, spell, hand] { MagicAction::EquipSpellInHand(player, spell, hand); });
    }

    void MagicState::ToggleAutomatic(int slot) {
        if (_holdActive || _autoActive) {
            return;
        }

        if (slot < 0 || slot >= 4) {
            return;
        }

        EnterAutomatic(slot);
    }

    void MagicState::PumpAutomatic() {
        if (!_autoActive || !_autoWaitingChargeComplete) {
            return;
        }

        auto* player = GetPlayer();
        if (!player) {
            ExitAll();
            return;
        }

        const std::uint32_t spellFormID = MagicSlots::GetSlotSpell(_autoSlot);
        if (spellFormID == 0) {
            StopAutoAttack();
            ExitAll();
            return;
        }

        auto const* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
        if (!spell) {
            StopAutoAttack();
            ExitAll();
            return;
        }

        auto* leftCaster = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        const float charge = spell->GetChargeTime();

        auto charged = [&](RE::ActorMagicCaster* caster) {
            if (!caster) {
                return false;
            }

            if (charge <= 0.0f) {
                return true;
            }

            const auto st = caster->state.get();

            if (st == RE::MagicCaster::State::kReady) {
                return true;
            }
            if (st == RE::MagicCaster::State::kCharging) {
                return caster->castingTimer + 1e-3f >= charge;
            }

            return false;
        };

        bool done = false;
        switch (_autoHand) {
            case EquipHand::Left:
                done = charged(leftCaster);
                break;
            case EquipHand::Right:
                done = charged(rightCaster);
                break;
            case EquipHand::Both:
            default:
                done = charged(leftCaster) && charged(rightCaster);
                break;
        }

        if (done) {
            _autoWaitingChargeComplete = false;
            StopAutoAttack();
            _autoChargeComplete = true;
        }
    }

    void MagicState::SetModeSpellsFromHand(EquipHand hand, RE::SpellItem* spell) {
        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;

        if (!spell) {
            return;
        }

        switch (hand) {
            using enum EquipHand;
            case Left:
                _modeSpellLeft = spell;
                break;
            case Right:
                _modeSpellRight = spell;
                break;
            case Both:
                _modeSpellLeft = spell;
                _modeSpellRight = spell;
                break;
            default:
                break;
        }
    }

    void MagicState::EnsureActiveWithSnapshot(RE::PlayerCharacter* player, int slot) {
        if (_active) {
            return;
        }

        CaptureSnapshot(player);
        _prevExtraEquipped.clear();

        _active = true;
        _activeSlot = slot;
    }

    bool MagicState::PrepareSlotSpell(int slot, bool checkMagicka, RE::PlayerCharacter*& player, RE::SpellItem*& spell,
                                      SpellSettings& outSettings) {
        player = GetPlayer();
        if (!player) {
            return false;
        }

        const std::uint32_t spellFormID = MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) {
            return false;
        }

        spell = RE::TESForm::LookupByID<RE::SpellItem>(spellFormID);
        if (!spell) {
            return false;
        }

        outSettings = SpellSettingsDB::Get().GetOrCreate(spellFormID);

        if (checkMagicka && !HasEnoughMagickaForSpell(player, spell, outSettings.hand)) {
            return false;
        }

        EnsureActiveWithSnapshot(player, slot);
        SetModeSpellsFromHand(outSettings.hand, spell);

        return true;
    }
}
