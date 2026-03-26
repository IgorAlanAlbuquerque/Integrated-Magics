#include "Assign.h"

#include "Config/Config.h"
#include "Config/Slots.h"
#include "PCH.h"
#include "State/State.h"

namespace IntegratedMagic::MagicAssign {

    static std::atomic<RE::FormID> s_lastEquippedMagicFormID{0};

    namespace {

        static bool IsAssociatedBoundWeaponOfSlot(RE::FormID weaponFormID, int activeSlot) {
            if (activeSlot < 0 || !weaponFormID) return false;

            const RE::FormID slotIDs[2] = {
                Slots::GetSlotSpell(activeSlot, Slots::Hand::Left),
                Slots::GetSlotSpell(activeSlot, Slots::Hand::Right),
            };

            for (const auto spellID : slotIDs) {
                if (!spellID) continue;
                const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
                if (!spell) continue;

                for (const auto* effect : spell->effects) {
                    if (!effect || !effect->baseEffect) continue;
                    const auto* associated = effect->baseEffect->data.associatedForm;
                    if (associated && associated->GetFormID() == weaponFormID) {
#ifdef DEBUG
                        spdlog::info(
                            "[Assign] IsAssociatedBoundWeaponOfSlot: weaponID={:#010x} matched associatedForm "
                            "of spell={:#010x} in slot={}",
                            weaponFormID, spellID, activeSlot);
#endif
                        return true;
                    }
                }
            }
            return false;
        }

        class MagicEquipSink : public RE::BSTEventSink<RE::TESEquipEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event,
                                                  RE::BSTEventSource<RE::TESEquipEvent>*) override {
                if (!a_event || !a_event->equipped) return RE::BSEventNotifyControl::kContinue;
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || a_event->actor.get() != player) return RE::BSEventNotifyControl::kContinue;

                const auto formID = a_event->baseObject;
                auto* form = RE::TESForm::LookupByID(formID);
                if (!form) return RE::BSEventNotifyControl::kContinue;

                if (form->As<RE::TESShout>() || form->As<RE::SpellItem>()) {
                    s_lastEquippedMagicFormID.store(formID, std::memory_order_relaxed);
#ifdef DEBUG
                    spdlog::info("[Assign] EquipSink: cached formID={:#010x}", formID);
#endif
                }

                auto& state = MagicState::Get();
                if (!state.IsActive()) return RE::BSEventNotifyControl::kContinue;

                if (auto const* spell = form->As<RE::SpellItem>()) {
                    const int activeSlot = state.ActiveSlot();
                    if (activeSlot >= 0) {
                        const auto lID = Slots::GetSlotSpell(activeSlot, Slots::Hand::Left);
                        const auto rID = Slots::GetSlotSpell(activeSlot, Slots::Hand::Right);
                        const auto sID = Slots::GetSlotShout(activeSlot);
                        if (formID == lID || formID == rID || formID == sID) return RE::BSEventNotifyControl::kContinue;
                    }
                    if (state.IsInSlotSetup()) return RE::BSEventNotifyControl::kContinue;
#ifdef DEBUG
                    spdlog::info(
                        "[Assign] EquipSink: foreign spell {:#010x} equipped during active slot {} -> "
                        "ForceExitNoRestore",
                        formID, state.ActiveSlot());
#endif
                    if (auto* task = SKSE::GetTaskInterface()) {
                        task->AddTask([]() { MagicState::Get().ForceExitNoRestore(); });
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (form->As<RE::TESObjectWEAP>() || form->As<RE::TESObjectARMO>() || form->As<RE::TESObjectMISC>()) {
                    if (form->As<RE::TESObjectWEAP>()) {
                        const int activeSlot = state.ActiveSlot();
                        if (IsAssociatedBoundWeaponOfSlot(formID, activeSlot)) {
#ifdef DEBUG
                            spdlog::info(
                                "[Assign] EquipSink: weaponID={:#010x} is bound weapon of active slot {} -> ignoring",
                                formID, activeSlot);
#endif
                            return RE::BSEventNotifyControl::kContinue;
                        }
                    }
#ifdef DEBUG
                    spdlog::info(
                        "[Assign] EquipSink: foreign item {:#010x} (weapon/armor/misc) equipped during active slot {} "
                        "-> ForceExitNoRestore",
                        formID, state.ActiveSlot());
#endif
                    if (auto* task = SKSE::GetTaskInterface()) {
                        task->AddTask([]() { MagicState::Get().ForceExitNoRestore(); });
                    }
                }

                return RE::BSEventNotifyControl::kContinue;
            }

            static MagicEquipSink* GetSingleton() {
                static MagicEquipSink inst;
                return &inst;
            }
        };

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

            if (!id) id = s_lastEquippedMagicFormID.load(std::memory_order_relaxed);

            return id;
        }
    }

    static constexpr RE::FormID kRightHandSlotID = 0x00013F42u;
    static constexpr RE::FormID kLeftHandSlotID = 0x00013F43u;
    static constexpr RE::FormID kEitherHandSlotID = 0x00013F44u;
    static constexpr RE::FormID kBothHandsSlotID = 0x00013F45u;

    static RE::FormID GetSpellEquipSlotID(const RE::SpellItem* spell) {
        if (!spell) return 0;
        const auto* slot = spell->GetEquipSlot();
        return slot ? slot->GetFormID() : 0;
    }

    bool IsTwoHandedSpell(const RE::SpellItem* spell) {
        if (!spell) return false;
        using ST = RE::MagicSystem::SpellType;
        const auto t = spell->GetSpellType();
        if (t == ST::kPower || t == ST::kLesserPower || t == ST::kVoicePower) return false;
        return GetSpellEquipSlotID(spell) == kBothHandsSlotID;
    }

    bool IsRightHandOnlySpell(const RE::SpellItem* spell) {
        return spell && GetSpellEquipSlotID(spell) == kRightHandSlotID;
    }

    bool IsLeftHandOnlySpell(const RE::SpellItem* spell) {
        return spell && GetSpellEquipSlotID(spell) == kLeftHandSlotID;
    }

    HoveredMagicType GetHoveredMagicType() {
        using enum IntegratedMagic::MagicAssign::HoveredMagicType;
        const auto formID = GetHoveredFormID();
        if (!formID) {
#ifdef DEBUG
            spdlog::info("[Assign] GetHoveredMagicType: no hovered formID -> None");
#endif
            return None;
        }

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) {
#ifdef DEBUG
            spdlog::info("[Assign] GetHoveredMagicType: formID={:#010x} not found -> None", formID);
#endif
            return None;
        }

        if (form->As<RE::TESShout>()) {
            return Shout;
        }

        if (auto const* spell = form->As<RE::SpellItem>()) {
            const auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || t == RE::MagicSystem::SpellType::kLesserPower) {
                return Power;
            }
            if (IsTwoHandedSpell(spell)) return TwoHandedSpell;
            if (IsRightHandOnlySpell(spell)) return RightOnlySpell;
            if (IsLeftHandOnlySpell(spell)) return LeftOnlySpell;
            return Spell;
        }
#ifdef DEBUG
        spdlog::info("[Assign] GetHoveredMagicType: formID={:#010x} unrecognised form type -> None", formID);
#endif
        return None;
    }

    bool TryAssignHoveredSpellToSlot(int slot, Slots::Hand hand) {
        const auto formID = GetHoveredFormID();
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

        if (IsTwoHandedSpell(spell)) {
#ifdef DEBUG
            spdlog::info(
                "[Assign] TryAssignHoveredSpellToSlot: slot={} spellID={:#010x} name='{}' -> TwoHanded: storing Left, "
                "clearing Right",
                slot, spell->GetFormID(), spell->GetFullName() ? spell->GetFullName() : "<null>");
#endif
            Slots::SetSlotSpell(slot, Slots::Hand::Left, spell->GetFormID(), true);
            Slots::SetSlotSpell(slot, Slots::Hand::Right, 0, true);
            Slots::SetSlotShout(slot, 0, true);
            return true;
        }

        const auto existingLeftID = Slots::GetSlotSpell(slot, Slots::Hand::Left);
        auto* existingLeftSpell = existingLeftID ? RE::TESForm::LookupByID<RE::SpellItem>(existingLeftID) : nullptr;
        if (hand == Slots::Hand::Right && existingLeftSpell && IsTwoHandedSpell(existingLeftSpell)) {
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
        const auto formID = GetHoveredFormID();
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
            const auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || t == RE::MagicSystem::SpellType::kLesserPower) {
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

    void RegisterEquipListener() {
        if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
            src->AddEventSink<RE::TESEquipEvent>(MagicEquipSink::GetSingleton());
            spdlog::info("[Assign] TESEquipEvent sink registered.");
        }
    }

    void ClearLastEquippedMagic() { s_lastEquippedMagicFormID.store(0, std::memory_order_relaxed); }
}