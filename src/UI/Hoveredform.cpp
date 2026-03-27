#include "HoveredForm.h"

#include "PCH.h"
#include "State/EquipSink.h"
#include "State/SpellClassify.h"

namespace IntegratedMagic::HoveredForm {
    namespace {
        RE::FormID TryGFxFormID(RE::GFxMovieView* movie, const char* path) {
            RE::GFxValue v;
            movie->GetVariable(&v, path);

            if (v.GetType() == RE::GFxValue::ValueType::kNumber) return static_cast<RE::FormID>(v.GetNumber());

            if (v.GetType() == RE::GFxValue::ValueType::kString) {
                const char* s = v.GetString();
                if (!s || !*s) return 0;
                char* end = nullptr;
                const auto id = static_cast<RE::FormID>(std::strtoul(s, &end, 0));
                return (end && end != s) ? id : 0;
            }

            return 0;
        }

    }

    RE::FormID GetHoveredFormID() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) return 0;

        auto menu = ui->GetMenu<RE::MagicMenu>();
        if (!menu || !menu->uiMovie) return 0;

        auto* movie = menu->uiMovie.get();

        RE::FormID id = TryGFxFormID(movie, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
        if (!id) id = TryGFxFormID(movie, "_root.Menu_mc.itemList.selectedEntry.formId");
        if (!id) id = TryGFxFormID(movie, "_root.Menu_mc.List_mc.selectedEntry.formId");
        if (!id) id = TryGFxFormID(movie, "_root.Menu_mc.selectedEntry.formId");

        if (!id) id = EquipSink::GetLastEquippedMagicFormID();

        return id;
    }

    MagicType GetHoveredMagicType() {
        const auto formID = GetHoveredFormID();
        if (!formID) {
#ifdef DEBUG
            spdlog::info("[HoveredForm] GetHoveredMagicType: no hovered formID -> None");
#endif
            return MagicType::None;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
#ifdef DEBUG
            spdlog::info("[HoveredForm] GetHoveredMagicType: formID={:#010x} not found -> None", formID);
#endif
            return MagicType::None;
        }

        if (form->As<RE::TESShout>()) return MagicType::Shout;

        if (auto const* spell = form->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            const auto t = spell->GetSpellType();
            if (t == ST::kPower || t == ST::kLesserPower) return MagicType::Power;

            using namespace SpellClassify;
            if (IsTwoHandedSpell(spell)) return MagicType::TwoHandedSpell;
            if (IsRightHandOnlySpell(spell)) return MagicType::RightOnlySpell;
            if (IsLeftHandOnlySpell(spell)) return MagicType::LeftOnlySpell;
            return MagicType::Spell;
        }

#ifdef DEBUG
        spdlog::info("[HoveredForm] GetHoveredMagicType: formID={:#010x} unrecognised form type -> None", formID);
#endif
        return MagicType::None;
    }

}