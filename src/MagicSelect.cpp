#include "MagicSelect.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/S/SpellItem.h>

#include "MagicInput.h"
#include "MagicSlots.h"
#include "MagicStrings.h"
#include "PCH.h"

namespace IntegratedMagic::MagicSelect {
    namespace {
        thread_local int g_suppressDepth = 0;  // NOSONAR
        bool IsSelectionSuppressed() { return g_suppressDepth > 0; }
    }

    ScopedSuppressSelection::ScopedSuppressSelection() { ++g_suppressDepth; }

    ScopedSuppressSelection::~ScopedSuppressSelection() { --g_suppressDepth; }

    bool TrySelectSpellFromEquip(RE::SpellItem* spell, MagicSlots::Hand hand) {
        if (IsSelectionSuppressed()) {
            return false;
        }

        const auto slotOpt = MagicInput::GetDownSlotForSelection();
        if (!slotOpt.has_value() || !spell) {
            return false;
        }

        const auto formID = spell->GetFormID();
        if (formID == 0) {
            return false;
        }

        ScopedSuppressSelection guard;
        IntegratedMagic::MagicSlots::SetSlotSpell(*slotOpt, hand, formID, true);
        return true;
    }

    bool TryClearSlotSpellFromUnequip(RE::SpellItem* spell, MagicSlots::Hand hand) {
        if (IsSelectionSuppressed()) {
            return false;
        }

        const auto slotOpt = MagicInput::GetDownSlotForSelection();
        if (!slotOpt.has_value() || !spell) {
            return false;
        }

        const auto id = spell->GetFormID();
        if (id == 0) {
            return false;
        }

        if (const auto cur = IntegratedMagic::MagicSlots::GetSlotSpell(*slotOpt, hand); cur != id) {
            return false;
        }

        ScopedSuppressSelection guard;
        IntegratedMagic::MagicSlots::SetSlotSpell(*slotOpt, hand, 0u, true);
        return true;
    }
}