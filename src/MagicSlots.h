#pragma once
#include <cstdint>

namespace IntegratedMagic::MagicSlots {
    enum class Hand : std::uint8_t { Left, Right };
    std::uint32_t GetSlotCount();
    bool IsValidSlot(int slot);
    std::uint32_t GetSlotSpell(int slot, Hand hand);
    void SetSlotSpell(int slot, Hand hand, std::uint32_t spellFormID, bool saveNow);
}