#include "Assign.h"

#include "Config/Config.h"
#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAssign {

    namespace {
        RE::FormID GetHoveredFormID() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) return 0;
            auto menu = ui->GetMenu<RE::MagicMenu>();
            if (!menu || !menu->uiMovie) return 0;

            RE::GFxValue result;
            menu->uiMovie->GetVariable(&result, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
            if (result.GetType() != RE::GFxValue::ValueType::kNumber) return 0;
            return static_cast<RE::FormID>(result.GetNumber());
        }
    }

    HoveredMagicType GetHoveredMagicType() {
        const auto formID = GetHoveredFormID();
        if (!formID) return HoveredMagicType::None;

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) return HoveredMagicType::None;

        if (form->As<RE::TESShout>()) return HoveredMagicType::Shout;

        if (auto* spell = form->As<RE::SpellItem>()) {
            const auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || t == RE::MagicSystem::SpellType::kLesserPower)
                return HoveredMagicType::Power;
            return HoveredMagicType::Spell;
        }
        return HoveredMagicType::None;
    }

    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand) {
        const auto formID = GetHoveredFormID();
        if (!formID) return false;

        auto* form = RE::TESForm::LookupByID(formID);
        auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
        if (!spell) return false;

        Slots::SetSlotSpell(slot, hand, spell->GetFormID(), /*saveNow=*/true);
        return true;
    }

    bool TryAssignHoveredShoutToSlot(int slot) {
        const auto formID = GetHoveredFormID();
        if (!formID) return false;

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) return false;

        if (form->As<RE::TESShout>()) {
            Slots::SetSlotShout(slot, formID, /*saveNow=*/true);
            return true;
        }

        if (auto* spell = form->As<RE::SpellItem>()) {
            const auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || t == RE::MagicSystem::SpellType::kLesserPower) {
                Slots::SetSlotShout(slot, formID, /*saveNow=*/true);
                return true;
            }
        }
        return false;
    }

    bool TryClearSlotHand(int slot, Slots::Hand hand) {
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
        Slots::SetSlotShout(slot, 0u, /*saveNow=*/true);
        return true;
    }
}