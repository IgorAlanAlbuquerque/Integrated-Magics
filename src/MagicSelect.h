#pragma once
#include <cstdint>

#include "PCH.h"

namespace RE {
    class Actor;
    class SpellItem;
    class BGSEquipSlot;
}

namespace IntegratedMagic::MagicSelect {
    bool TrySelectSpellFromEquip(RE::SpellItem* spell, MagicSlots::Hand src);
    bool TryClearSlotSpellFromUnequip(RE::SpellItem* spell, MagicSlots::Hand hand);
    bool IsSelectionSuppressed() noexcept;
    bool TrySelectShoutFromEquip(RE::TESForm* shoutOrPower);
    bool TryClearSlotShoutFromUnequip(RE::TESForm* shoutOrPower);
    class ScopedSuppressSelection {
    public:
        ScopedSuppressSelection() noexcept;
        ~ScopedSuppressSelection() noexcept;
        ScopedSuppressSelection(const ScopedSuppressSelection&) = delete;
        ScopedSuppressSelection& operator=(const ScopedSuppressSelection&) = delete;
        ScopedSuppressSelection(ScopedSuppressSelection&&) = delete;
        ScopedSuppressSelection& operator=(ScopedSuppressSelection&&) = delete;
    };
}