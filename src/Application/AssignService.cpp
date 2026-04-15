#include "AssignService.h"

#include "Config/ConfigAdapter.h"
#include "Domain/SpellClassify.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Shared/Hand.h"
#include "UI/HoveredForm.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Hand hand) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
            MAGIC_DEBUG_LOG("[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} - no hovered formID, abort", slot,
                            (hand == Hand::Left) ? "Left" : "Right");

            return false;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
        if (!spell) {
            MAGIC_DEBUG_LOG(
                "[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} - formID={:#010x} is not a SpellItem, abort",
                slot, (hand == Hand::Left) ? "Left" : "Right", formID);

            return false;
        }

        if (SpellClassify::IsTwoHandedSpell(spell)) {
            MAGIC_DEBUG_LOG(
                "[Assign] TryAssignHoveredSpellToSlot: slot={} spellID={:#010x} name='{}' "
                "-> TwoHanded: storing Left, clearing Right",
                slot, spell->GetFormID(), spell->GetFullName() ? spell->GetFullName() : "<null>");

            Slots::SetSlotSpell(slot, Hand::Left, spell->GetFormID(), true);
            Slots::SetSlotSpell(slot, Hand::Right, 0, true);
            Slots::SetSlotShout(slot, 0, true);
            return true;
        }

        const auto existingLeftID = Slots::GetSlotSpell(slot, Hand::Left);
        if (auto const* existingLeftSpell =
                existingLeftID ? RE::TESForm::LookupByID<RE::SpellItem>(existingLeftID) : nullptr;
            hand == Hand::Right && existingLeftSpell && SpellClassify::IsTwoHandedSpell(existingLeftSpell)) {
            Slots::SetSlotSpell(slot, Hand::Left, 0, true);
        }

        MAGIC_DEBUG_LOG("[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} spellID={:#010x} name='{}'", slot,
                        (hand == Hand::Left) ? "Left" : "Right", spell->GetFormID(),
                        spell->GetFullName() ? spell->GetFullName() : "<null>");

        Slots::SetSlotSpell(slot, hand, spell->GetFormID(), true);
        return true;
    }

    bool TryAssignHoveredShoutToSlot(int slot) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
            MAGIC_DEBUG_LOG("[Assign] TryAssignHoveredShoutToSlot: slot={} - no hovered formID, abort", slot);

            return false;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
            MAGIC_DEBUG_LOG("[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} not found, abort", slot,
                            formID);

            return false;
        }

        if (form->As<RE::TESShout>()) {
            MAGIC_DEBUG_LOG("[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} -> assigned as Shout", slot,
                            formID);

            Slots::SetSlotShout(slot, formID, true);
            return true;
        }

        if (auto const* spell = form->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            if (const auto t = spell->GetSpellType(); t == ST::kPower || t == ST::kLesserPower) {
                MAGIC_DEBUG_LOG(
                    "[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} spellType={} -> assigned as Power",
                    slot, formID, static_cast<int>(t));

                Slots::SetSlotShout(slot, formID, true);
                return true;
            }

            MAGIC_DEBUG_LOG(
                "[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} is a regular spell (not Power), abort",
                slot, formID);
        }

        return false;
    }

    bool TryClearSlotHand(int slot, Hand hand) {
        MAGIC_DEBUG_LOG("[Assign] TryClearSlotHand: slot={} hand={}", slot, (hand == Hand::Left) ? "Left" : "Right");

        auto& adapter = Config::MagicConfigAdapter::Get();
        adapter.SetSpell(slot, hand == Hand::Left, 0u);
        adapter.Save();
        return true;
    }

    bool TryClearSlotShout(int slot) {
        MAGIC_DEBUG_LOG("[Assign] TryClearSlotShout: slot={}", slot);

        Slots::SetSlotShout(slot, 0u, true);
        return true;
    }
}