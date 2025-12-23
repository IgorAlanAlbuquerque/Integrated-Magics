#pragma once
#include <cstdint>

namespace IntegratedMagic::MagicSlots {
    std::uint32_t GetSlotSpell(int slot);
    void SetSlotSpell(int slot, std::uint32_t spellFormID, bool saveNow = true);
}
