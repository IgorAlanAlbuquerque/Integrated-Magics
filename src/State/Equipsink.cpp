#include "EquipSink.h"

#include "Config/Slots.h"
#include "PCH.h"
#include "State/State.h"

namespace IntegratedMagic::EquipSink {

    static std::atomic<RE::FormID> s_lastEquippedMagicFormID{0};

    namespace {
        bool IsAssociatedBoundWeaponOfSlot(RE::FormID weaponFormID, int activeSlot) {
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
                            "[EquipSink] IsAssociatedBoundWeaponOfSlot: weaponID={:#010x} matched "
                            "associatedForm of spell={:#010x} in slot={}",
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
                    spdlog::info("[EquipSink] cached formID={:#010x}", formID);
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
                        "[EquipSink] foreign spell {:#010x} equipped during active slot {} -> ForceExitNoRestore",
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
                            spdlog::info("[EquipSink] weaponID={:#010x} is bound weapon of active slot {} -> ignoring",
                                         formID, activeSlot);
#endif
                            return RE::BSEventNotifyControl::kContinue;
                        }
                    }

                    if (state.IsInSlotSetup() || state.IsShoutActive()) return RE::BSEventNotifyControl::kContinue;
#ifdef DEBUG
                    spdlog::info(
                        "[EquipSink] foreign item {:#010x} (weapon/armor/misc) equipped during active slot {} "
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