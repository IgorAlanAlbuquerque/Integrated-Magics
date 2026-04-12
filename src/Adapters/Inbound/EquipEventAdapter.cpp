#ifdef GetObject
    #undef GetObject
#endif

#include "EquipEventAdapter.h"

#include <array>

#include "Application/SpellSystemController.h"
#include "Domain/Hand.h"
#include "PCH.h"
#include "Persistence/Slots.h"

namespace IntegratedMagic::EquipSink {

    static std::atomic<RE::FormID> s_lastEquippedMagicFormID{0};  // NOSONAR

    namespace {
        bool IsAssociatedBoundWeaponOfSlot(RE::FormID weaponFormID, int activeSlot) {
            if (activeSlot < 0 || !weaponFormID) return false;
            const std::array<RE::FormID, 2> slotIDs = {
                Slots::GetSlotSpell(activeSlot, Domain::Hand::Left),
                Slots::GetSlotSpell(activeSlot, Domain::Hand::Right),
            };
            for (const auto spellID : slotIDs) {
                if (!spellID) continue;
                const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
                if (!spell) continue;
                for (const auto* effect : spell->effects) {
                    if (!effect || !effect->baseEffect) continue;
                    const auto* associated = effect->baseEffect->data.associatedForm;
                    if (associated && associated->GetFormID() == weaponFormID) return true;
                }
            }
            return false;
        }

        void ScheduleForceExitNoRestore() {
            if (auto* task = SKSE::GetTaskInterface())
                task->AddTask([]() { Application::SpellSystemController::Get().NotifyForeignEquip(); });
        }

        class MagicEquipSink : public RE::BSTEventSink<RE::TESEquipEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event,
                                                  RE::BSTEventSource<RE::TESEquipEvent>*) override {
                if (!a_event || !a_event->equipped) return RE::BSEventNotifyControl::kContinue;

                auto const* player = RE::PlayerCharacter::GetSingleton();
                if (!player || a_event->actor.get() != player) return RE::BSEventNotifyControl::kContinue;

                const auto formID = a_event->baseObject;
                auto* form = RE::TESForm::LookupByID(formID);
                if (!form) return RE::BSEventNotifyControl::kContinue;

                if (form->As<RE::TESShout>() || form->As<RE::SpellItem>())
                    s_lastEquippedMagicFormID.store(formID, std::memory_order_relaxed);

                auto const& ctrl = Application::SpellSystemController::Get();
                if (!ctrl.IsSpellSystemActive()) return RE::BSEventNotifyControl::kContinue;

                if (auto const* spell = form->As<RE::SpellItem>()) {
                    const int activeSlot = ctrl.ActiveSlot();
                    if (activeSlot < 0 || ctrl.IsInSlotSetup()) return RE::BSEventNotifyControl::kContinue;

                    const auto lID = Slots::GetSlotSpell(activeSlot, Domain::Hand::Left);
                    const auto rID = Slots::GetSlotSpell(activeSlot, Domain::Hand::Right);
                    const auto sID = Slots::GetSlotShout(activeSlot);
                    if (formID == lID || formID == rID || formID == sID) return RE::BSEventNotifyControl::kContinue;

                    if (const bool isPower = spell->GetSpellType() == RE::MagicSystem::SpellType::kPower ||
                                             spell->GetSpellType() == RE::MagicSystem::SpellType::kLesserPower;
                        isPower) {
                        if (!sID) return RE::BSEventNotifyControl::kContinue;
                        MAGIC_DEBUG_LOG("[EquipSink] foreign power {:#010x} -> ForceExitNoRestore", formID);
                        ScheduleForceExitNoRestore();
                        return RE::BSEventNotifyControl::kContinue;
                    }

                    const RE::FormID rightNow =
                        player->GetEquippedEntryData(false) && player->GetEquippedEntryData(false)->GetObject()
                            ? player->GetEquippedEntryData(false)->GetObject()->GetFormID()
                            : 0;

                    if (const RE::FormID leftNow =
                            player->GetEquippedEntryData(true) && player->GetEquippedEntryData(true)->GetObject()
                                ? player->GetEquippedEntryData(true)->GetObject()->GetFormID()
                                : 0;
                        !(rightNow == formID && rID != 0) && !(leftNow == formID && lID != 0))
                        return RE::BSEventNotifyControl::kContinue;

                    MAGIC_DEBUG_LOG("[EquipSink] foreign spell {:#010x} conflicts -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (form->As<RE::TESShout>()) {
                    const int activeSlot = ctrl.ActiveSlot();
                    if (activeSlot < 0) return RE::BSEventNotifyControl::kContinue;
                    if (const auto sID = Slots::GetSlotShout(activeSlot); !sID || formID == sID)
                        return RE::BSEventNotifyControl::kContinue;
                    MAGIC_DEBUG_LOG("[EquipSink] foreign shout {:#010x} -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (form->As<RE::TESObjectWEAP>() || form->As<RE::TESObjectARMO>() || form->As<RE::TESObjectMISC>()) {
                    const int activeSlot = ctrl.ActiveSlot();
                    if (activeSlot < 0 || ctrl.IsInSlotSetup() || ctrl.IsShoutActive())
                        return RE::BSEventNotifyControl::kContinue;

                    if (form->As<RE::TESObjectWEAP>() && IsAssociatedBoundWeaponOfSlot(formID, activeSlot))
                        return RE::BSEventNotifyControl::kContinue;

                    const auto lID = Slots::GetSlotSpell(activeSlot, Domain::Hand::Left);
                    const auto rID = Slots::GetSlotSpell(activeSlot, Domain::Hand::Right);

                    if (auto const* armature = form->As<RE::TESObjectARMO>()) {
                        if (const bool isShield = armature->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kShield);
                            !isShield || !lID)
                            return RE::BSEventNotifyControl::kContinue;
                        MAGIC_DEBUG_LOG("[EquipSink] shield {:#010x} conflicts -> ForceExitNoRestore", formID);
                        ScheduleForceExitNoRestore();
                        return RE::BSEventNotifyControl::kContinue;
                    }

                    const RE::FormID rightNow =
                        player->GetEquippedEntryData(false) && player->GetEquippedEntryData(false)->GetObject()
                            ? player->GetEquippedEntryData(false)->GetObject()->GetFormID()
                            : 0;

                    if (const RE::FormID leftNow =
                            player->GetEquippedEntryData(true) && player->GetEquippedEntryData(true)->GetObject()
                                ? player->GetEquippedEntryData(true)->GetObject()->GetFormID()
                                : 0;
                        !(rightNow == formID && rID != 0) && !(leftNow == formID && lID != 0))
                        return RE::BSEventNotifyControl::kContinue;

                    MAGIC_DEBUG_LOG("[EquipSink] weapon/misc {:#010x} conflicts -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                }

                return RE::BSEventNotifyControl::kContinue;
            }

            static MagicEquipSink* GetSingleton() {
                static MagicEquipSink inst;  // NOSONAR
                return &inst;
            }
        };
    }

    RE::FormID GetLastEquippedMagicFormID() { return s_lastEquippedMagicFormID.load(std::memory_order_relaxed); }
    void ClearLastEquippedMagic() { s_lastEquippedMagicFormID.store(0, std::memory_order_relaxed); }

    void RegisterEquipListener() {
        if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
            src->AddEventSink<RE::TESEquipEvent>(MagicEquipSink::GetSingleton());
            spdlog::info("[EquipSink] TESEquipEvent sink registered.");
        }
    }
}