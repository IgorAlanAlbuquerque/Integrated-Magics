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

    class ScopedSuppressSelection {
    public:
        ScopedSuppressSelection();
        ~ScopedSuppressSelection();

        ScopedSuppressSelection(const ScopedSuppressSelection&) = delete;
        ScopedSuppressSelection& operator=(const ScopedSuppressSelection&) = delete;
    };
}