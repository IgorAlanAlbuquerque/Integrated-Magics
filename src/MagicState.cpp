#include "MagicState.h"

#include "MagicAction.h"
#include "MagicSlots.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic {
    namespace {
        inline RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }

        inline RE::TESBoundObject* AsBoundObject(RE::TESForm* f) { return f ? f->As<RE::TESBoundObject>() : nullptr; }
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

        if (_active) {
            if (_activeSlot == slot) {
                ExitPress();
                return;
            }
            ExitPress();
        }

        EnterPress(slot);
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

        const auto settings = SpellSettingsDB::Get().GetOrCreate(spellFormID);

        MagicAction::EquipSpellInHand(player, spell, settings.hand);

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

        if (auto* r = player->GetEquippedEntryData(false); r && r->GetObject()) {
            _snap.rightObject = r->GetObject();
        }
        if (auto* l = player->GetEquippedEntryData(true); l && l->GetObject()) {
            _snap.leftObject = l->GetObject();
        }

        if (auto* rightCaster = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kRightHand)) {
            _snap.rightSpell = rightCaster->currentSpell;
        }
        if (auto* leftCaster = MagicAction::GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand)) {
            _snap.leftSpell = leftCaster->currentSpell;
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

        {
            auto* cur = player->GetEquippedEntryData(false);
            auto* curObj = cur ? cur->GetObject() : nullptr;

            if (_snap.rightObject) {
                if (auto* obj = AsBoundObject(_snap.rightObject)) {
                    mgr->EquipObject(player, obj, nullptr, 1, nullptr, true, true, true, false);
                }
            } else {
                if (auto* obj = AsBoundObject(curObj)) {
                    mgr->UnequipObject(player, obj, nullptr, 1, nullptr, true, true, true, false, nullptr);
                }
            }
        }

        {
            auto* cur = player->GetEquippedEntryData(true);
            auto* curObj = cur ? cur->GetObject() : nullptr;

            if (_snap.leftObject) {
                if (auto* obj = AsBoundObject(_snap.leftObject)) {
                    mgr->EquipObject(player, obj, nullptr, 1, nullptr, true, true, true, false);
                }
            } else {
                if (auto* obj = AsBoundObject(curObj)) {
                    mgr->UnequipObject(player, obj, nullptr, 1, nullptr, true, true, true, false, nullptr);
                }
            }
        }

        auto* leftSpell = _snap.leftSpell ? _snap.leftSpell->As<RE::SpellItem>() : nullptr;
        auto* rightSpell = _snap.rightSpell ? _snap.rightSpell->As<RE::SpellItem>() : nullptr;

        if (!leftSpell && !rightSpell) {
            MagicAction::ClearHandSpell(player, EquipHand::Both);
            return;
        }

        if (leftSpell && rightSpell && leftSpell == rightSpell) {
            MagicAction::EquipSpellInHand(player, leftSpell, EquipHand::Both);
            return;
        }

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
}
