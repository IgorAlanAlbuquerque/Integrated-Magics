#include "Assign.h"

#include "Config/Config.h"
#include "PCH.h"

namespace IntegratedMagic::MagicAssign {

    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand) {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) return false;

        auto menu = ui->GetMenu<RE::MagicMenu>();
        if (!menu || !menu->uiMovie) return false;

        RE::GFxValue result;
        menu->uiMovie->GetVariable(&result, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");

        if (result.GetType() != RE::GFxValue::ValueType::kNumber) return false;

        auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(result.GetNumber()));
        auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
        if (!spell) return false;

        auto& cfg = GetMagicConfig();
        const auto s = static_cast<std::size_t>(slot);

        if (hand == Slots::Hand::Right)
            cfg.slotSpellFormIDRight[s].store(spell->GetFormID(), std::memory_order_relaxed);
        else
            cfg.slotSpellFormIDLeft[s].store(spell->GetFormID(), std::memory_order_relaxed);

        cfg.Save();
        return true;
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
}