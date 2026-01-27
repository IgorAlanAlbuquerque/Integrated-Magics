#include "MagicState.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "MagicAction.h"
#include "MagicEquipSlots.h"
#include "MagicSelect.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace detail {
        static SyntheticInputState& GetSynth() {
            static SyntheticInputState s;  // NOSONAR
            return s;
        }

        static RE::ButtonEvent* MakeAttackButtonEvent(bool leftHand, float value, float heldSecs) {
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

        void DispatchAttack(IntegratedMagic::MagicSlots::Hand hand, float value, float heldSecs) {
            using enum IntegratedMagic::MagicSlots::Hand;
            if (hand == Left) {
                EnqueueSyntheticAttack(MakeAttackButtonEvent(true, value, heldSecs));
            } else {
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

            const auto preferred = leftHand ? RE::ExtraDataType::kWornLeft : RE::ExtraDataType::kWorn;

            RE::ExtraDataList* firstNonNull = nullptr;
            RE::ExtraDataList* anyWorn = nullptr;

            for (auto* x : *entry->extraLists) {
                if (!x) {
                    continue;
                }

                if (!firstNonNull) {
                    firstNonNull = x;
                }

                if (x->HasType(preferred)) {
                    return x;
                }

                if (!anyWorn && (x->HasType(RE::ExtraDataType::kWorn) || x->HasType(RE::ExtraDataType::kWornLeft))) {
                    anyWorn = x;
                }
            }

            return anyWorn ? anyWorn : firstNonNull;
        }

        RE::ExtraDataList* ResolveLiveExtra(const InventoryIndex& idx, RE::TESBoundObject* base,
                                            RE::ExtraDataList* candidate) {
            if (!base || !candidate) {
                return nullptr;
            }

            auto it = idx.extrasByBase.find(base);
            if (it == idx.extrasByBase.end()) {
                return nullptr;
            }

            for (auto* ex : it->second) {
                if (ex == candidate) {
                    return ex;
                }
            }
            return nullptr;
        }

        RE::ExtraDataList* FindAnyInstanceExtraForBase(const InventoryIndex& idx, RE::TESBoundObject* base) {
            if (!base) {
                return nullptr;
            }

            auto it = idx.extrasByBase.find(base);
            if (it == idx.extrasByBase.end() || it->second.empty()) {
                return nullptr;
            }
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

            return (leftObj == base) || (rightObj == base);
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

                if (it.base->GetFormType() == RE::FormType::Weapon) continue;

                RE::ExtraDataList* liveExtra = ResolveLiveExtra(idx, it.base, it.extra);
                if (!liveExtra) {
                    liveExtra = FindAnyInstanceExtraForBase(idx, it.base);
                }

                const bool isArmor = (it.base->GetFormType() == RE::FormType::Armor);

                mgr->EquipObject(actor, it.base, liveExtra, 1, nullptr,
                                 /*queue*/ true,
                                 /*force*/ false,
                                 /*sounds*/ true,
                                 /*applyNow*/ isArmor);
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

        float GetSpellMagickaCost(RE::PlayerCharacter* player, RE::SpellItem* spell) {
            if (!player || !spell) {
                return 0.0f;
            }
            return spell->CalculateMagickaCost(player);
        }

        bool HasEnoughMagickaForSpell(RE::PlayerCharacter* player, RE::SpellItem* spell) {
            const float mag = GetPlayerMagicka(player);
            const float cost = GetSpellMagickaCost(player, spell);

            if (cost <= 0.0f) {
                return true;
            }

            return (mag + 1e-2f) >= cost;
        }

        inline RE::SpellItem* AsSpell(RE::MagicItem* m) { return m ? m->As<RE::SpellItem>() : nullptr; }

        inline void ClearHandSpellIfNoSnapshot(RE::PlayerCharacter* player, RE::SpellItem* snapSpell,
                                               RE::SpellItem* modeSpell, IntegratedMagic::MagicSlots::Hand hand) {
            if (snapSpell) {
                return;
            }

            if (modeSpell) {
                IntegratedMagic::MagicAction::ClearHandSpell(player, modeSpell, hand);
            } else {
                IntegratedMagic::MagicAction::ClearHandSpell(player, hand);
            }
        }

        inline void EquipSpellIfPresent(RE::PlayerCharacter* player, RE::SpellItem* spell,
                                        IntegratedMagic::MagicSlots::Hand hand) {
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
        InventoryIndex idx{};
        if (!player) {
            return idx;
        }

        auto inv = player->GetInventory([](RE::TESBoundObject&) { return true; });
        for (auto const& [obj, data] : inv) {
            auto* base = obj;
            auto const* entry = data.second.get();
            if (!base || !entry || !entry->extraLists) {
                continue;
            }

            auto& vec = idx.extrasByBase[base];
            for (auto* extra : *entry->extraLists) {
                if (!extra) {
                    continue;
                }

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

    void MagicState::EnsureActiveWithSnapshot(RE::PlayerCharacter* player, int slot) {
        if (_active) {
            _activeSlot = slot;
            return;
        }

        CaptureSnapshot(player);
        _prevExtraEquipped.clear();

        _active = true;
        _activeSlot = slot;

        _left = {};
        _right = {};

        _aaHeldLeft = false;
        _aaHeldRight = false;
        _aaSecsLeft = 0.0f;
        _aaSecsRight = 0.0f;

        _attackEnabled = false;

        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;
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

        auto GetEquippedSpellFromHand = [](RE::Actor* a, bool leftHand) -> RE::SpellItem* {
            if (!a) {
                return nullptr;
            }
            auto* f = a->GetEquippedObject(leftHand);
            return f ? f->As<RE::SpellItem>() : nullptr;
        };

        if (player->GetEquippedEntryData(false)) {
            _snap.rightSpell = nullptr;
        } else {
            _snap.rightSpell = GetEquippedSpellFromHand(player, /*leftHand*/ false);
        }

        if (player->GetEquippedEntryData(true)) {
            _snap.leftSpell = nullptr;
        } else {
            _snap.leftSpell = GetEquippedSpellFromHand(player, /*leftHand*/ true);
        }

        _snap.valid = true;
    }

    void MagicState::RestoreSnapshot(RE::PlayerCharacter* player) {
        using enum IntegratedMagic::MagicSlots::Hand;

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

        auto* leftSnapSpell = (_snap.leftObj.base ? nullptr : AsSpell(_snap.leftSpell));
        auto* rightSnapSpell = (_snap.rightObj.base ? nullptr : AsSpell(_snap.rightSpell));

        if (_dirtyRight) {
            ClearHandSpellIfNoSnapshot(player, rightSnapSpell, _modeSpellRight, Right);

            RestoreOneHand(player, mgr, idx, false, _snap.rightObj, rightSlot);
            EquipSpellIfPresent(player, rightSnapSpell, Right);
        }

        if (_dirtyLeft) {
            ClearHandSpellIfNoSnapshot(player, leftSnapSpell, _modeSpellLeft, Left);

            RestoreOneHand(player, mgr, idx, true, _snap.leftObj, leftSlot);
            EquipSpellIfPresent(player, leftSnapSpell, Left);
        }

        auto idx2 = BuildInventoryIndex(player);
        ReequipPrevExtraEquipped(player, mgr, idx2, _prevExtraEquipped);

        _snap.valid = false;
        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;
        _dirtyLeft = false;
        _dirtyRight = false;
    }

    bool MagicState::PrepareSlotEntry(int slot, SlotEntry& out) {
        out = {};

        auto* player = GetPlayer();
        if (!player) {
            return false;
        }

        if (!IntegratedMagic::MagicSlots::IsValidSlot(slot)) {
            return false;
        }

        using enum IntegratedMagic::MagicSlots::Hand;

        out.player = player;
        out.rightID = IntegratedMagic::MagicSlots::GetSlotSpell(slot, Right);
        out.leftID = IntegratedMagic::MagicSlots::GetSlotSpell(slot, Left);

        out.rightSpell = out.rightID ? RE::TESForm::LookupByID<RE::SpellItem>(out.rightID) : nullptr;
        out.leftSpell = out.leftID ? RE::TESForm::LookupByID<RE::SpellItem>(out.leftID) : nullptr;

        out.hasRight = (out.rightSpell != nullptr);
        out.hasLeft = (out.leftSpell != nullptr);

        if (!out.hasRight && !out.hasLeft) {
            return false;
        }

        if (out.hasRight) {
            out.rightSettings = SpellSettingsDB::Get().GetOrCreate(out.rightID);
        }
        if (out.hasLeft) {
            out.leftSettings = SpellSettingsDB::Get().GetOrCreate(out.leftID);
        }

        EnsureActiveWithSnapshot(player, slot);

        _modeSpellRight = out.rightSpell;
        _modeSpellLeft = out.leftSpell;

        if (out.hasRight) {
            _right.mode = out.rightSettings.mode;
            _right.wantAutoAttack = out.rightSettings.autoAttack;
        } else {
            _right = {};
        }

        if (out.hasLeft) {
            _left.mode = out.leftSettings.mode;
            _left.wantAutoAttack = out.leftSettings.autoAttack;
        } else {
            _left = {};
        }

        return true;
    }

    void MagicState::SetModeSpellsFromHand(IntegratedMagic::MagicSlots::Hand hand, RE::SpellItem* spell) {
        using enum IntegratedMagic::MagicSlots::Hand;

        if (hand == Left) {
            _modeSpellLeft = spell;
        } else {
            _modeSpellRight = spell;
        }
    }

    void MagicState::EnterHand(IntegratedMagic::MagicSlots::Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);

        hm.finished = false;
        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;

        hm.waitingChargeComplete = false;
        hm.chargeComplete = false;
        hm.waitingAutoAfterEquip = false;

        hm.pressActive = false;
        hm.holdActive = false;
        hm.autoActive = false;

        using enum IntegratedMagic::ActivationMode;

        switch (ss.mode) {
            case Hold:
                hm.holdActive = true;
                if (hm.wantAutoAttack) {
                    hm.waitingAutoAfterEquip = true;
                    _attackEnabled = false;
                }
                break;

            case Automatic:
                hm.autoActive = true;
                hm.waitingChargeComplete = true;
                hm.waitingAutoAfterEquip = true;
                hm.chargeComplete = false;
                hm.wantAutoAttack = true;
                hm.waitingEnableBumperSecs = 0.0f;
                _attackEnabled = false;
                break;

            case Press:

                hm.pressActive = true;

                break;
        }
    }

    void MagicState::TogglePressHand(IntegratedMagic::MagicSlots::Hand hand, const SpellSettings& ss) {
        auto& hm = ModeFor(hand);

        hm.mode = ss.mode;
        hm.wantAutoAttack = ss.autoAttack;

        hm.pressActive = !hm.pressActive;
        if (!hm.pressActive) {
            FinishHand(hand);
        }
    }

    void MagicState::FinishHand(IntegratedMagic::MagicSlots::Hand hand) {
        auto& hm = ModeFor(hand);

        hm.finished = true;
        hm.holdActive = false;
        hm.autoActive = false;
        hm.pressActive = false;

        hm.waitingAutoAfterEquip = false;
        hm.waitingChargeComplete = false;

        StopAutoAttack(hand);
    }

    void MagicState::OnSlotPressed(int slot) {
        if (_active && slot == _activeSlot) {
            using enum IntegratedMagic::MagicSlots::Hand;

            const bool needL = (_modeSpellLeft != nullptr);
            const bool needR = (_modeSpellRight != nullptr);

            const bool pressL = needL && (_left.mode == IntegratedMagic::ActivationMode::Press) && _left.pressActive;
            const bool pressR = needR && (_right.mode == IntegratedMagic::ActivationMode::Press) && _right.pressActive;

            if (!pressL && !pressR) {
                return;
            }

            if (pressL && pressR) {
                FinishHand(Left);
                FinishHand(Right);

                ExitAllNow();
                return;
            }

            if (pressL) {
                FinishHand(Left);
            }
            if (pressR) {
                FinishHand(Right);
            }

            TryFinalizeExit();
            return;
        }

        if (_active && slot != _activeSlot) {
            if (!CanOverwriteNow()) {
                return;
            }
            PrepareForOverwriteToSlot(slot);
        }

        SlotEntry e{};
        if (!PrepareSlotEntry(slot, e)) {
            return;
        }
        using enum IntegratedMagic::MagicSlots::Hand;

        if (e.hasRight && e.rightSettings.mode == IntegratedMagic::ActivationMode::Automatic &&
            !HasEnoughMagickaForSpell(e.player, e.rightSpell)) {
            e.hasRight = false;
            DisableHand(IntegratedMagic::MagicSlots::Hand::Right);
        }

        if (e.hasLeft && e.leftSettings.mode == IntegratedMagic::ActivationMode::Automatic &&
            !HasEnoughMagickaForSpell(e.player, e.leftSpell)) {
            e.hasLeft = false;
            DisableHand(IntegratedMagic::MagicSlots::Hand::Left);
        }

        if (!e.hasRight) {
            DisableHand(Right);
            SetModeSpellsFromHand(Right, nullptr);
        }
        if (!e.hasLeft) {
            DisableHand(Left);
            SetModeSpellsFromHand(Left, nullptr);
        }

        if (!e.hasLeft && !e.hasRight) {
            ExitAllNow();
            return;
        }

        auto* player = e.player;
        UpdatePrevExtraEquippedForOverlay([&] {
            if (e.hasRight) {
                IntegratedMagic::MagicAction::EquipSpellInHand(player, e.rightSpell, Right);
                MarkDirty(Right);
            }
            if (e.hasLeft) {
                IntegratedMagic::MagicAction::EquipSpellInHand(player, e.leftSpell, Left);
                MarkDirty(Left);
            }
        });

        if (e.hasRight) {
            SetModeSpellsFromHand(Right, e.rightSpell);
            EnterHand(Right, e.rightSettings);
        } else {
            _right = {};
        }

        if (e.hasLeft) {
            SetModeSpellsFromHand(Left, e.leftSpell);
            EnterHand(Left, e.leftSettings);
        } else {
            _left = {};
        }
    }

    void MagicState::OnSlotReleased(int slot) {
        if (!_active || slot != _activeSlot) {
            return;
        }

        using enum IntegratedMagic::MagicSlots::Hand;

        if (_left.holdActive) {
            _left.holdActive = false;
            FinishHand(Left);
        }

        if (_right.holdActive) {
            _right.holdActive = false;
            FinishHand(Right);
        }

        TryFinalizeExit();
    }

    void MagicState::StartAutoAttack(IntegratedMagic::MagicSlots::Hand hand) {
        using enum IntegratedMagic::MagicSlots::Hand;

        if (hand == Left) {
            _aaHeldLeft = true;
            _aaSecsLeft = 0.0f;
        } else {
            _aaHeldRight = true;
            _aaSecsRight = 0.0f;
        }

        IntegratedMagic::detail::DispatchAttack(hand, 1.0f, 0.0f);
    }

    void MagicState::StopAutoAttack(IntegratedMagic::MagicSlots::Hand hand) {
        using enum IntegratedMagic::MagicSlots::Hand;

        bool& held = (hand == Left) ? _aaHeldLeft : _aaHeldRight;
        float& secs = (hand == Left) ? _aaSecsLeft : _aaSecsRight;

        if (!held) {
            return;
        }

        const float heldSecs = (secs > 0.0f) ? secs : 0.1f;
        IntegratedMagic::detail::DispatchAttack(hand, 0.0f, heldSecs);

        held = false;
        secs = 0.0f;
    }

    void MagicState::StopAllAutoAttack() {
        using enum IntegratedMagic::MagicSlots::Hand;
        StopAutoAttack(Left);
        StopAutoAttack(Right);
    }

    void MagicState::PumpAutoAttack(float dt) {
        using enum IntegratedMagic::MagicSlots::Hand;

        const float add = (dt > 0.0f) ? dt : 0.0f;

        if (_aaHeldLeft) {
            _aaSecsLeft += add;
            IntegratedMagic::detail::DispatchAttack(Left, 1.0f, _aaSecsLeft);
        }

        if (_aaHeldRight) {
            _aaSecsRight += add;
            IntegratedMagic::detail::DispatchAttack(Right, 1.0f, _aaSecsRight);
        }
    }

    void MagicState::NotifyAttackEnabled() {
        if (!_active) {
            return;
        }

        _attackEnabled = true;

        using enum IntegratedMagic::MagicSlots::Hand;

        if (_left.waitingAutoAfterEquip) {
            _left.waitingAutoAfterEquip = false;
            if ((_left.autoActive || _left.wantAutoAttack) && !_aaHeldLeft) {
                StartAutoAttack(Left);
            }
        }

        if (_right.waitingAutoAfterEquip) {
            _right.waitingAutoAfterEquip = false;
            if ((_right.autoActive || _right.wantAutoAttack) && !_aaHeldRight) {
                StartAutoAttack(Right);
            }
        }
    }

    void MagicState::PumpAutomatic(float dt) {
        using enum IntegratedMagic::MagicSlots::Hand;
        PumpAutoStartFallback(Left, dt);
        PumpAutoStartFallback(Right, dt);
        PumpAutomaticHand(Left);
        PumpAutomaticHand(Right);
    }

    void MagicState::PumpAutoStartFallback(IntegratedMagic::MagicSlots::Hand hand, float dt) {
        using enum IntegratedMagic::MagicSlots::Hand;
        auto& hm = ModeFor(hand);

        if (!_active || !hm.autoActive || !hm.waitingAutoAfterEquip) {
            return;
        }

        hm.waitingEnableBumperSecs += (dt > 0.0f ? dt : 0.0f);

        constexpr float kFallbackDelay = 0.25f;

        if (hm.waitingEnableBumperSecs >= kFallbackDelay) {
            hm.waitingAutoAfterEquip = false;

            if (hand == Left) {
                if (!_aaHeldLeft) StartAutoAttack(Left);
            } else {
                if (!_aaHeldRight) StartAutoAttack(Right);
            }
        }
    }

    void MagicState::PumpAutomaticHand(IntegratedMagic::MagicSlots::Hand hand) {
        auto& hm = ModeFor(hand);

        if (!hm.autoActive || !hm.waitingChargeComplete) {
            return;
        }

        auto* player = GetPlayer();
        if (!player) {
            FinishHand(hand);
            return;
        }

        if (!_active || _activeSlot < 0) {
            FinishHand(hand);
            return;
        }

        const std::uint32_t id = IntegratedMagic::MagicSlots::GetSlotSpell(_activeSlot, hand);
        if (id == 0u) {
            FinishHand(hand);
            return;
        }

        auto const* spell = RE::TESForm::LookupByID<RE::SpellItem>(id);
        if (!spell) {
            FinishHand(hand);
            return;
        }

        const auto src = (hand == IntegratedMagic::MagicSlots::Hand::Left) ? RE::MagicSystem::CastingSource::kLeftHand
                                                                           : RE::MagicSystem::CastingSource::kRightHand;

        auto* caster = IntegratedMagic::MagicAction::GetCaster(player, src);

        const float charge = spell->GetChargeTime();

        auto charged = [&](RE::ActorMagicCaster* c) {
            if (!c) {
                return false;
            }
            if (charge <= 0.0f) {
                return true;
            }

            const auto st = c->state.get();

            if (st == RE::MagicCaster::State::kReady) {
                return true;
            }
            if (st == RE::MagicCaster::State::kCharging) {
                return (c->castingTimer + 1e-3f) >= charge;
            }

            return false;
        };

        if (!charged(caster)) {
            return;
        }

        hm.waitingChargeComplete = false;
        hm.chargeComplete = true;

        StopAutoAttack(hand);
    }

    bool MagicState::HandIsRelevant(IntegratedMagic::MagicSlots::Hand h) const {
        using enum IntegratedMagic::MagicSlots::Hand;
        return (h == Left) ? (_modeSpellLeft != nullptr) : (_modeSpellRight != nullptr);
    }

    bool MagicState::AllRelevantHandsFinished() const {
        using enum IntegratedMagic::MagicSlots::Hand;

        const bool needL = HandIsRelevant(Left);
        const bool needR = HandIsRelevant(Right);

        const bool okL = !needL || _left.finished;
        const bool okR = !needR || _right.finished;

        return okL && okR;
    }

    void MagicState::TryFinalizeExit() {
        if (!_active) {
            return;
        }

        if (!AllRelevantHandsFinished()) {
            return;
        }

        ExitAllNow();
    }

    void MagicState::ExitAllNow() {
        auto* player = GetPlayer();
        if (!player) {
            _active = false;
            _activeSlot = -1;
            _left = {};
            _right = {};
            _modeSpellLeft = nullptr;
            _modeSpellRight = nullptr;
            _snap.valid = false;
            return;
        }

        StopAllAutoAttack();
        RestoreSnapshot(player);

        if (auto* mgr = RE::ActorEquipManager::GetSingleton(); mgr) {
            auto idx = BuildInventoryIndex(player);
            ReequipPrevExtraEquipped(player, mgr, idx, _prevExtraEquipped);
        }

        _active = false;
        _activeSlot = -1;

        _left = {};
        _right = {};

        _aaHeldLeft = false;
        _aaHeldRight = false;
        _aaSecsLeft = 0.f;
        _aaSecsRight = 0.f;

        _attackEnabled = false;

        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;
        _snap.valid = false;
    }

    void MagicState::OnCastStop() {
        if (!_active) {
            return;
        }

        using enum IntegratedMagic::MagicSlots::Hand;
        if (_left.autoActive && !_left.finished) {
            if (_left.chargeComplete) {
                FinishHand(Left);
            }
        }

        if (_right.autoActive && !_right.finished) {
            if (_right.chargeComplete) {
                FinishHand(Right);
            }
        }
        TryFinalizeExit();
    }

    bool MagicState::CanOverwriteNow() const {
        if (!_active || _activeSlot < 0) {
            return false;
        }

        using enum IntegratedMagic::MagicSlots::Hand;

        const bool needL = (_modeSpellLeft != nullptr);
        const bool needR = (_modeSpellRight != nullptr);

        if (!needL && !needR) {
            return false;
        }

        if ((needL && (_left.holdActive || _left.autoActive)) || (needR && (_right.holdActive || _right.autoActive))) {
            return false;
        }

        int pressCount = 0;
        if (needL && _left.mode == IntegratedMagic::ActivationMode::Press) {
            ++pressCount;
        }
        if (needR && _right.mode == IntegratedMagic::ActivationMode::Press) {
            ++pressCount;
        }
        if (pressCount == 0) {
            return false;
        }

        if (needL && _left.mode != IntegratedMagic::ActivationMode::Press && !_left.finished) {
            return false;
        }
        if (needR && _right.mode != IntegratedMagic::ActivationMode::Press && !_right.finished) {
            return false;
        }

        return true;
    }

    void MagicState::PrepareForOverwriteToSlot(int newSlot) {
        StopAllAutoAttack();

        _activeSlot = newSlot;

        _left = {};
        _right = {};

        _aaHeldLeft = false;
        _aaHeldRight = false;
        _aaSecsLeft = 0.f;
        _aaSecsRight = 0.f;

        _attackEnabled = false;

        _modeSpellLeft = nullptr;
        _modeSpellRight = nullptr;
    }

    void MagicState::DisableHand(IntegratedMagic::MagicSlots::Hand hand) {
        auto& hm = ModeFor(hand);
        StopAutoAttack(hand);
        hm = {};
        hm.finished = true;
        SetModeSpellsFromHand(hand, nullptr);
    }
}