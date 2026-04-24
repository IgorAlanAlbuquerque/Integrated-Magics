#include "Persistence/Slots.h"

#include "Config/ConfigAdapter.h"
#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"

namespace IntegratedMagic::Slots {
    std::uint32_t GetSlotCount() { return static_cast<std::uint32_t>(Config::MagicConfigAdapter::Get().SlotCount()); }

    bool IsValidSlot(int slot) {
        if (slot < 0) return false;
        return slot < Config::MagicConfigAdapter::Get().SlotCount();
    }

    std::uint32_t GetSlotSpell(int slot, Hand hand) {
        return Config::MagicConfigAdapter::Get().GetSpell(slot, hand == Hand::Left);
    }

    void SetSlotSpell(int slot, Hand hand, std::uint32_t spellFormID, bool saveNow) {
        auto& adapter = Config::MagicConfigAdapter::Get();
        if (!IsValidSlot(slot)) return;

        adapter.SetSpell(slot, hand == Hand::Left, spellFormID);

        if (spellFormID != 0u) {
            auto const* form = RE::TESForm::LookupByID(spellFormID);
            (void)SpellSettingsDB::Get().GetOrCreate(spellFormID, form, &adapter);
        }

        if (saveNow) {
            adapter.Save();

            if (SpellSettingsDB::Get().IsDirty()) {
                SpellSettingsDB::Get().Save();
                SpellSettingsDB::Get().ClearDirty();
            }
        }
    }

    std::uint32_t GetSlotShout(int slot) { return Config::MagicConfigAdapter::Get().GetShout(slot); }

    void SetSlotShout(int slot, std::uint32_t shoutFormID, bool saveNow) {
        auto& adapter = Config::MagicConfigAdapter::Get();
        if (!IsValidSlot(slot)) return;

        adapter.SetShout(slot, shoutFormID);

        if (shoutFormID != 0u) {
            auto const* form = RE::TESForm::LookupByID(shoutFormID);
            (void)SpellSettingsDB::Get().GetOrCreate(shoutFormID, form, &adapter);
        }

        if (saveNow) {
            adapter.Save();
            if (SpellSettingsDB::Get().IsDirty()) {
                SpellSettingsDB::Get().Save();
                SpellSettingsDB::Get().ClearDirty();
            }
        }
    }

    bool IsShoutSlot(int slot) { return GetSlotShout(slot) != 0u; }
}