#include "Assign.h"

#include "Config/Config.h"
#include "Config/Slots.h"
#include "PCH.h"
#include "State/SpellClassify.h"
#include "UI/HoveredForm.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
#ifdef DEBUG
            spdlog::info("[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} - no hovered formID, abort", slot,
                         (hand == Slots::Hand::Left) ? "Left" : "Right");
#endif
            return false;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
        if (!spell) {
#ifdef DEBUG
            spdlog::info(
                "[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} - formID={:#010x} is not a SpellItem, abort",
                slot, (hand == Slots::Hand::Left) ? "Left" : "Right", formID);
#endif
            return false;
        }

        if (SpellClassify::IsTwoHandedSpell(spell)) {
#ifdef DEBUG
            spdlog::info(
                "[Assign] TryAssignHoveredSpellToSlot: slot={} spellID={:#010x} name='{}' "
                "-> TwoHanded: storing Left, clearing Right",
                slot, spell->GetFormID(), spell->GetFullName() ? spell->GetFullName() : "<null>");
#endif
            Slots::SetSlotSpell(slot, Slots::Hand::Left, spell->GetFormID(), true);
            Slots::SetSlotSpell(slot, Slots::Hand::Right, 0, true);
            Slots::SetSlotShout(slot, 0, true);
            return true;
        }

        const auto existingLeftID = Slots::GetSlotSpell(slot, Slots::Hand::Left);
        auto* existingLeftSpell = existingLeftID ? RE::TESForm::LookupByID<RE::SpellItem>(existingLeftID) : nullptr;
        if (hand == Slots::Hand::Right && existingLeftSpell && SpellClassify::IsTwoHandedSpell(existingLeftSpell)) {
            Slots::SetSlotSpell(slot, Slots::Hand::Left, 0, true);
        }

#ifdef DEBUG
        spdlog::info("[Assign] TryAssignHoveredSpellToSlot: slot={} hand={} spellID={:#010x} name='{}'", slot,
                     (hand == Slots::Hand::Left) ? "Left" : "Right", spell->GetFormID(),
                     spell->GetFullName() ? spell->GetFullName() : "<null>");
#endif
        Slots::SetSlotSpell(slot, hand, spell->GetFormID(), true);
        return true;
    }

    bool TryAssignHoveredShoutToSlot(int slot) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
#ifdef DEBUG
            spdlog::info("[Assign] TryAssignHoveredShoutToSlot: slot={} - no hovered formID, abort", slot);
#endif
            return false;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
#ifdef DEBUG
            spdlog::info("[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} not found, abort", slot,
                         formID);
#endif
            return false;
        }

        if (form->As<RE::TESShout>()) {
#ifdef DEBUG
            spdlog::info("[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} -> assigned as Shout", slot,
                         formID);
#endif
            Slots::SetSlotShout(slot, formID, true);
            return true;
        }

        if (auto const* spell = form->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            const auto t = spell->GetSpellType();
            if (t == ST::kPower || t == ST::kLesserPower) {
#ifdef DEBUG
                spdlog::info(
                    "[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} spellType={} -> assigned as Power",
                    slot, formID, static_cast<int>(t));
#endif
                Slots::SetSlotShout(slot, formID, true);
                return true;
            }
#ifdef DEBUG
            spdlog::info(
                "[Assign] TryAssignHoveredShoutToSlot: slot={} formID={:#010x} is a regular spell (not Power), abort",
                slot, formID);
#endif
        }

        return false;
    }

    bool TryClearSlotHand(int slot, Slots::Hand hand) {
#ifdef DEBUG
        spdlog::info("[Assign] TryClearSlotHand: slot={} hand={}", slot,
                     (hand == Slots::Hand::Left) ? "Left" : "Right");
#endif
        auto& cfg = GetMagicConfig();
        const auto s = static_cast<std::size_t>(slot);

        if (hand == Slots::Hand::Right)
            cfg.slotSpellFormIDRight[s].store(0, std::memory_order_relaxed);
        else
            cfg.slotSpellFormIDLeft[s].store(0, std::memory_order_relaxed);
        cfg.Save();
        return true;
    }

    bool TryClearSlotShout(int slot) {
#ifdef DEBUG
        spdlog::info("[Assign] TryClearSlotShout: slot={}", slot);
#endif
        Slots::SetSlotShout(slot, 0u, true);
        return true;
    }
}