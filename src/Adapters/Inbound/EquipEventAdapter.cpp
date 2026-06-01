#ifdef GetObject
    #undef GetObject
#endif

#include "Adapters/Inbound/EquipEventAdapter.h"

#include <array>

#include "Application/SpellSystemController.h"
#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::EquipSink {

    static std::atomic<RE::FormID> s_lastEquippedMagicFormID{0};  // NOSONAR

    namespace {
        using Contents = Application::SpellSystemController::ActiveSlotContents;

        bool IsAssociatedBoundWeaponOfSlot(RE::FormID weaponFormID, const Contents& contents) {
            if (!weaponFormID) return false;
            const std::array<RE::FormID, 2> slotIDs = {contents.leftSpell, contents.rightSpell};
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
                using enum RE::BSEventNotifyControl;
                if (!a_event || !a_event->equipped) return kContinue;

                auto const* player = RE::PlayerCharacter::GetSingleton();
                if (!player || a_event->actor.get() != player) return kContinue;

                const auto formID = a_event->baseObject;
                auto* form = RE::TESForm::LookupByID(formID);
                if (!form) return kContinue;

                if (form->As<RE::TESShout>() || form->As<RE::SpellItem>())
                    s_lastEquippedMagicFormID.store(formID, std::memory_order_relaxed);

                auto const& ctrl = Application::SpellSystemController::Get();
                if (!ctrl.IsSpellSystemActive()) return kContinue;

                if (auto const* spell = form->As<RE::SpellItem>()) {
                    if (ctrl.ActiveSlot() < 0 || ctrl.IsInSlotSetup()) return kContinue;

                    const auto contents = ctrl.GetActiveSlotContents();
                    const auto lID = contents.leftSpell;
                    const auto rID = contents.rightSpell;
                    const auto sID = contents.shout;
                    if (formID == lID || formID == rID || formID == sID) return kContinue;

                    if (const bool isPower = spell->GetSpellType() == RE::MagicSystem::SpellType::kPower ||
                                             spell->GetSpellType() == RE::MagicSystem::SpellType::kLesserPower;
                        isPower) {
                        if (!sID) return kContinue;
                        MAGIC_DEBUG_LOG("[EquipSink] foreign power {:#010x} -> ForceExitNoRestore", formID);
                        ScheduleForceExitNoRestore();
                        return kContinue;
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
                        return kContinue;

                    MAGIC_DEBUG_LOG("[EquipSink] foreign spell {:#010x} conflicts -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                    return kContinue;
                }

                if (form->As<RE::TESShout>()) {
                    if (ctrl.ActiveSlot() < 0) return kContinue;
                    if (const auto sID = ctrl.GetActiveSlotContents().shout; !sID || formID == sID)
                        return kContinue;
                    MAGIC_DEBUG_LOG("[EquipSink] foreign shout {:#010x} -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                    return kContinue;
                }

                if (form->As<RE::TESObjectWEAP>() || form->As<RE::TESObjectARMO>() || form->As<RE::TESObjectMISC>()) {
                    if (ctrl.ActiveSlot() < 0 || ctrl.IsInSlotSetup() || ctrl.IsShoutActive())
                        return kContinue;

                    const auto contents = ctrl.GetActiveSlotContents();
                    if (form->As<RE::TESObjectWEAP>() && IsAssociatedBoundWeaponOfSlot(formID, contents))
                        return kContinue;

                    const auto lID = contents.leftSpell;
                    const auto rID = contents.rightSpell;

                    if (auto const* armature = form->As<RE::TESObjectARMO>()) {
                        if (const bool isShield = armature->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kShield);
                            !isShield || !lID)
                            return kContinue;
                        MAGIC_DEBUG_LOG("[EquipSink] shield {:#010x} conflicts -> ForceExitNoRestore", formID);
                        ScheduleForceExitNoRestore();
                        return kContinue;
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
                        return kContinue;

                    MAGIC_DEBUG_LOG("[EquipSink] weapon/misc {:#010x} conflicts -> ForceExitNoRestore", formID);
                    ScheduleForceExitNoRestore();
                }

                return kContinue;
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