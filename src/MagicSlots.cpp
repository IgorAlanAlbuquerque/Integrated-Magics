#include "MagicSlots.h"

#include "MagicConfig.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic::MagicSlots {
    std::uint32_t GetSlotCount() { return IntegratedMagic::GetMagicConfig().SlotCount(); }

    bool IsValidSlot(int slot) {
        if (slot < 0) {
            return false;
        }
        const auto n = IntegratedMagic::GetMagicConfig().SlotCount();
        return static_cast<std::uint32_t>(slot) < n;
    }

    std::uint32_t GetSlotSpell(int slot) {
        auto& cfg = IntegratedMagic::GetMagicConfig();

        if (const auto n = cfg.SlotCount(); slot < 0 || static_cast<std::uint32_t>(slot) >= n) {
            return 0u;
        }

        return cfg.slotSpellFormID[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
    }

    void SetSlotSpell(int slot, std::uint32_t spellFormID, bool saveNow) {
        auto& cfg = IntegratedMagic::GetMagicConfig();

        if (const auto n = cfg.SlotCount(); slot < 0 || static_cast<std::uint32_t>(slot) >= n) {
            return;
        }

        cfg.slotSpellFormID[static_cast<std::size_t>(slot)].store(spellFormID, std::memory_order_relaxed);

        if (spellFormID != 0u) {
            (void)IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(spellFormID);
        }

        if (saveNow) {
            cfg.Save();

            if (IntegratedMagic::SpellSettingsDB::Get().IsDirty()) {
                IntegratedMagic::SpellSettingsDB::Get().Save();
                IntegratedMagic::SpellSettingsDB::Get().ClearDirty();
            }
        }
    }
}