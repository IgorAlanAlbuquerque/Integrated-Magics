#include "MagicSlots.h"

#include "MagicConfig.h"
#include "PCH.h"
#include "SpellSettingsDB.h"

namespace IntegratedMagic::MagicSlots {
    namespace {
        inline auto& SlotArrForHand(MagicConfig& cfg, Hand hand) {
            return (hand == Hand::Left) ? cfg.slotSpellFormIDLeft : cfg.slotSpellFormIDRight;
        }
    }
    std::uint32_t GetSlotCount() { return IntegratedMagic::GetMagicConfig().SlotCount(); }

    bool IsValidSlot(int slot) {
        if (slot < 0) {
            return false;
        }
        const auto n = IntegratedMagic::GetMagicConfig().SlotCount();
        return static_cast<std::uint32_t>(slot) < n;
    }

    std::uint32_t GetSlotSpell(int slot, Hand hand) {
        auto& cfg = IntegratedMagic::GetMagicConfig();
        if (const auto n = cfg.SlotCount(); slot < 0 || static_cast<std::uint32_t>(slot) >= n) {
            return 0u;
        }
        auto& arr = SlotArrForHand(cfg, hand);
        return arr[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
    }

    void SetSlotSpell(int slot, Hand hand, std::uint32_t spellFormID, bool saveNow) {
        auto& cfg = IntegratedMagic::GetMagicConfig();
        if (const auto n = cfg.SlotCount(); slot < 0 || static_cast<std::uint32_t>(slot) >= n) {
            return;
        }
        auto& arr = SlotArrForHand(cfg, hand);
        arr[static_cast<std::size_t>(slot)].store(spellFormID, std::memory_order_relaxed);
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