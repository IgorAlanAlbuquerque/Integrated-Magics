#include "MagicSlots.h"

#include "MagicConfig.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic::MagicSlots {
    static std::atomic<std::uint32_t>& SlotRef(IntegratedMagic::MagicConfig& cfg, int slot) {
        switch (slot) {
            case 0:
                return cfg.slotSpellFormID1;
            case 1:
                return cfg.slotSpellFormID2;
            case 2:
                return cfg.slotSpellFormID3;
            case 3:
                return cfg.slotSpellFormID4;
            default:
                return cfg.slotSpellFormID1;
        }
    }

    std::uint32_t GetSlotSpell(int slot) {
        if (slot < 0 || slot > 3) {
            return 0;
        }
        auto& cfg = IntegratedMagic::GetMagicConfig();
        return SlotRef(cfg, slot).load(std::memory_order_relaxed);
    }

    void SetSlotSpell(int slot, std::uint32_t spellFormID, bool saveNow) {
        if (slot < 0 || slot > 3) {
            return;
        }

        auto& cfg = IntegratedMagic::GetMagicConfig();
        SlotRef(cfg, slot).store(spellFormID, std::memory_order_relaxed);

        if (spellFormID != 0) {
            (void)IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(spellFormID);
        }

        if (saveNow) {
            cfg.Save();
            if (const bool dirty = IntegratedMagic::SpellSettingsDB::Get().IsDirty(); dirty) {
                IntegratedMagic::SpellSettingsDB::Get().Save();
                IntegratedMagic::SpellSettingsDB::Get().ClearDirty();
            }
        }
    }
}
