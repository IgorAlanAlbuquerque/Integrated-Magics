#include "MagicSelect.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/S/SpellItem.h>

#include "MagicInput.h"
#include "MagicSlots.h"
#include "MagicStrings.h"
#include "PCH.h"

namespace IntegratedMagic::MagicSelect {
    namespace {
        thread_local bool g_suppressSelection = false;  // NOSONAR
    }

    ScopedSuppressSelection::ScopedSuppressSelection() {
        _prev = g_suppressSelection;
        g_suppressSelection = true;
    }

    ScopedSuppressSelection::~ScopedSuppressSelection() { g_suppressSelection = _prev; }

    bool TrySelectSpellFromEquip(RE::Actor* actor, RE::SpellItem* spell) {
        if (!actor || !spell) {
            return false;
        }

        if (g_suppressSelection) {
            return false;
        }

        if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player || actor != player) {
            return false;
        }

        const std::uint32_t formID = spell->GetFormID();

        const auto slotOpt = MagicInput::GetDownSlotForSelection();
        if (!slotOpt.has_value()) {
            return false;
        }

        const int slot = *slotOpt;
        if (formID == 0) {
            return false;
        }

        IntegratedMagic::MagicSlots::SetSlotSpell(slot, formID, true);

        return true;
    }
}
