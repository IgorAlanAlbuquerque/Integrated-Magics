#include "Shared/AssignService.h"

#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/HoveredFormState.h"
#include "Shared/SpellClassify.h"

namespace IntegratedMagic::MagicAssign {

    std::optional<SlotMutation> ComputeSpellAssignment(int slot, Hand hand, RE::FormID existingLeftID) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
            MAGIC_DEBUG_LOG("[Assign] ComputeSpellAssignment: slot={} hand={} - no hovered formID, abort", slot,
                            (hand == Hand::Left) ? "Left" : "Right");
            return std::nullopt;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
        if (!spell) {
            MAGIC_DEBUG_LOG(
                "[Assign] ComputeSpellAssignment: slot={} hand={} - formID={:#010x} is not a SpellItem, abort", slot,
                (hand == Hand::Left) ? "Left" : "Right", formID);
            return std::nullopt;
        }

        SlotMutation m;
        m.slot = slot;

        if (SpellClassify::IsTwoHandedSpell(spell)) {
            MAGIC_DEBUG_LOG(
                "[Assign] ComputeSpellAssignment: slot={} spellID={:#010x} name='{}' "
                "-> TwoHanded: Left=formID, Right=0, Shout=0",
                slot, spell->GetFormID(), spell->GetFullName() ? spell->GetFullName() : "<null>");
            m.leftSpell = spell->GetFormID();
            m.rightSpell = 0u;
            m.shout = 0u;
            return m;
        }

        if (hand == Hand::Right && existingLeftID) {
            auto const* existingLeft = RE::TESForm::LookupByID<RE::SpellItem>(existingLeftID);
            if (existingLeft && SpellClassify::IsTwoHandedSpell(existingLeft)) m.leftSpell = 0u;
        }

        MAGIC_DEBUG_LOG("[Assign] ComputeSpellAssignment: slot={} hand={} spellID={:#010x} name='{}'", slot,
                        (hand == Hand::Left) ? "Left" : "Right", spell->GetFormID(),
                        spell->GetFullName() ? spell->GetFullName() : "<null>");

        if (hand == Hand::Left)
            m.leftSpell = spell->GetFormID();
        else
            m.rightSpell = spell->GetFormID();

        return m;
    }

    std::optional<SlotMutation> ComputeShoutAssignment(int slot) {
        const auto formID = HoveredForm::GetHoveredFormID();
        if (!formID) {
            MAGIC_DEBUG_LOG("[Assign] ComputeShoutAssignment: slot={} - no hovered formID, abort", slot);
            return std::nullopt;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
            MAGIC_DEBUG_LOG("[Assign] ComputeShoutAssignment: slot={} formID={:#010x} not found, abort", slot, formID);
            return std::nullopt;
        }

        SlotMutation m;
        m.slot = slot;

        if (form->As<RE::TESShout>()) {
            MAGIC_DEBUG_LOG("[Assign] ComputeShoutAssignment: slot={} formID={:#010x} -> Shout", slot, formID);
            m.shout = formID;
            return m;
        }

        if (auto const* spell = form->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            if (const auto t = spell->GetSpellType(); t == ST::kPower || t == ST::kLesserPower) {
                MAGIC_DEBUG_LOG("[Assign] ComputeShoutAssignment: slot={} formID={:#010x} spellType={} -> Power", slot,
                                formID, static_cast<int>(t));
                m.shout = formID;
                return m;
            }
            MAGIC_DEBUG_LOG("[Assign] ComputeShoutAssignment: slot={} formID={:#010x} is regular spell, abort", slot,
                            formID);
        }

        return std::nullopt;
    }

    SlotMutation ComputeClearHand(int slot, Hand hand) {
        MAGIC_DEBUG_LOG("[Assign] ComputeClearHand: slot={} hand={}", slot, (hand == Hand::Left) ? "Left" : "Right");
        SlotMutation m;
        m.slot = slot;
        if (hand == Hand::Left)
            m.leftSpell = 0u;
        else
            m.rightSpell = 0u;
        return m;
    }

    SlotMutation ComputeClearShout(int slot) {
        MAGIC_DEBUG_LOG("[Assign] ComputeClearShout: slot={}", slot);
        SlotMutation m;
        m.slot = slot;
        m.shout = 0u;
        return m;
    }

}
