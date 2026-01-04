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
    }

    ScopedSuppressSelection::ScopedSuppressSelection() { ++g_suppressDepth; }
    ScopedSuppressSelection::~ScopedSuppressSelection() { --g_suppressDepth; }

    static bool IsSelectionSuppressed() { return g_suppressDepth > 0; }

    bool TrySelectSpellFromEquip(RE::SpellItem* spell) {
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
        IntegratedMagic::MagicSlots::SetSlotSpell(*slotOpt, formID, true);
        return true;
    }
}
