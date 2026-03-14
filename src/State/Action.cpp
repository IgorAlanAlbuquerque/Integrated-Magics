#include "Action.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "Config/Config.h"
#include "Config/EquipSlots.h"

namespace IntegratedMagic::MagicAction {
    namespace {
        inline void SetCasterDual(RE::ActorMagicCaster* caster, bool dual) {
            if (caster) {
                caster->SetDualCasting(dual);
            }
        }

        RE::MagicSystem::CastingSource ToCastingSource(Slots::Hand hand) {
            return (hand == Slots::Hand::Left) ? RE::MagicSystem::CastingSource::kLeftHand
                                               : RE::MagicSystem::CastingSource::kRightHand;
        }

        const RE::BGSEquipSlot* ToEquipSlot(Slots::Hand hand) {
            return IntegratedMagic::EquipUtil::GetHandEquipSlot(hand);
        }

        int ToUnEquipHandInt(Slots::Hand hand) { return (hand == Slots::Hand::Left) ? 0 : 1; }

        void UnEquipSpell(RE::PlayerCharacter* pc, RE::SpellItem* spell, int hand) {
            auto aeMan = RE::ActorEquipManager::GetSingleton();
            if (!aeMan || !pc || !spell) {
                return;
            }
            using func_t = void (RE::ActorEquipManager::*)(RE::Actor*, RE::SpellItem*, int);
            REL::Relocation<func_t> func{RELOCATION_ID(37947, 38903)};
            func(aeMan, pc, spell, hand);
        }

        void UnEquipShout(RE::Actor* a_actor, RE::TESShout* a_shout) {
            auto aeMan = RE::ActorEquipManager::GetSingleton();
            if (!aeMan || !a_actor || !a_shout) {
                return;
            }
            using func_t = void (RE::ActorEquipManager::*)(RE::Actor*, RE::TESShout*);
            REL::Relocation<func_t> func{RELOCATION_ID(37948, 38904)};
            func(aeMan, a_actor, a_shout);
        }

        RE::SpellItem* GetEquippedSpellFromCaster(RE::ActorMagicCaster* caster) {
            if (!caster || !caster->currentSpell) {
                return nullptr;
            }
            return caster->currentSpell->As<RE::SpellItem>();
        }

        const RE::BSFixedString kInstantAnim("InstantEquipAnim");

        inline void SetSkipEquipVars(RE::PlayerCharacter* pc, bool enable) {
            if (!pc) {
                return;
            }
            (void)pc->SetGraphVariableBool(kInstantAnim, enable);
        }

        inline std::atomic<std::uint64_t> g_skipToken{0};

        inline void ScheduleDisableSkipEquip(std::uint64_t token, int delayMs) {
            std::thread([token, delayMs]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

                if (g_skipToken.load(std::memory_order_relaxed) != token) {
                    return;
                }

                if (auto* task = SKSE::GetTaskInterface()) {
                    task->AddTask([token]() {
                        if (g_skipToken.load(std::memory_order_relaxed) != token) {
                            return;
                        }
                        auto* pc = RE::PlayerCharacter::GetSingleton();
                        SetSkipEquipVars(pc, false);
                    });
                }
            }).detach();
        }

        bool IsPowerSpell(RE::TESForm* form) {
            auto const* spell = form ? form->As<RE::SpellItem>() : nullptr;
            if (!spell) return false;
            using ST = RE::MagicSystem::SpellType;
            return spell->GetSpellType() == ST::kPower || spell->GetSpellType() == ST::kLesserPower;
        }
    }

    RE::ActorMagicCaster* GetCaster(RE::PlayerCharacter* player, RE::MagicSystem::CastingSource source) {
        if (!player) {
            return nullptr;
        }
        auto* mc = player->GetMagicCaster(source);
        return mc ? skyrim_cast<RE::ActorMagicCaster*>(mc) : nullptr;
    }

    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Slots::Hand hand) {
        if (!player || !spell) {
            return;
        }
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            return;
        }
        auto* caster = GetCaster(player, ToCastingSource(hand));
        SetCasterDual(caster, false);
#ifdef DEBUG
        spdlog::info("[Action] EquipSpellInHand: hand={} spellID={:#010x} name='{}' | currentCasterSpell={:#010x}",
                     (hand == Slots::Hand::Left) ? "Left" : "Right", spell->GetFormID(),
                     spell->GetFullName() ? spell->GetFullName() : "<null>",
                     (caster && caster->currentSpell) ? caster->currentSpell->GetFormID() : 0u);
#endif

        auto const& cfg = IntegratedMagic::GetMagicConfig();
        std::uint64_t token = 0;

        if (cfg.skipEquipAnimationPatch) {
            token = g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1;
            SetSkipEquipVars(player, true);
            ScheduleDisableSkipEquip(token, 500);
        }

        const auto* equipSlot = ToEquipSlot(hand);
        mgr->EquipSpell(player, spell, equipSlot);
    }

    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Slots::Hand hand) {
        if (!player || !spell) {
            return;
        }
#ifdef DEBUG
        spdlog::info("[Action] ClearHandSpell(spell): hand={} spellID={:#010x}",
                     (hand == Slots::Hand::Left) ? "Left" : "Right", spell->GetFormID());
#endif
        auto* caster = GetCaster(player, ToCastingSource(hand));
        SetCasterDual(caster, false);
        UnEquipSpell(player, spell, ToUnEquipHandInt(hand));
    }

    void ClearHandSpell(RE::PlayerCharacter* player, Slots::Hand hand) {
        if (!player) {
            return;
        }
        auto* caster = GetCaster(player, ToCastingSource(hand));
        auto* cur = GetEquippedSpellFromCaster(caster);
        if (!cur) {
#ifdef DEBUG
            spdlog::info("[Action] ClearHandSpell(no-spell): hand={} - caster has no spell, skipping",
                         (hand == Slots::Hand::Left) ? "Left" : "Right");
#endif
            return;
        }
#ifdef DEBUG
        spdlog::info("[Action] ClearHandSpell(no-spell): hand={} clearing spellID={:#010x}",
                     (hand == Slots::Hand::Left) ? "Left" : "Right", cur->GetFormID());
#endif
        ClearHandSpell(player, cur, hand);
    }

    void EquipShoutInVoice(RE::PlayerCharacter* player, RE::TESForm* shoutOrPower) {
        if (!player || !shoutOrPower) return;
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) return;

        if (auto* shout = shoutOrPower->As<RE::TESShout>()) {
            mgr->EquipShout(player, shout);
        } else if (auto* spell = shoutOrPower->As<RE::SpellItem>(); spell && IsPowerSpell(shoutOrPower)) {
            mgr->EquipSpell(player, spell, nullptr);
        }
    }

    void ClearVoiceShout(RE::PlayerCharacter* player) {
        if (!player) return;
        if (auto* shout = player->GetCurrentShout()) {
            UnEquipShout(player, shout);
            return;
        }
        auto const& rd = player->GetActorRuntimeData();
        if (auto* power = rd.selectedPower ? rd.selectedPower->As<RE::SpellItem>() : nullptr) {
            if (IsPowerSpell(power)) {
                UnEquipSpell(player, power, 2);
                UnEquipSpell(player, power, 1);
                UnEquipSpell(player, power, 0);
            }
        }
    }

    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player) {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        if (!cfg.skipEquipAnimationOnReturnPatch) {
            return;
        }
        const std::uint64_t token = g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1;
        SetSkipEquipVars(player, true);
        ScheduleDisableSkipEquip(token, 500);
    }

    void EquipSlotContent(RE::PlayerCharacter* player, int slot) {
        using enum IntegratedMagic::Slots::Hand;
        if (!player) return;

        if (IntegratedMagic::Slots::IsShoutSlot(slot)) {
            const auto shoutID = IntegratedMagic::Slots::GetSlotShout(slot);
            if (auto* form = shoutID ? RE::TESForm::LookupByID(shoutID) : nullptr) EquipShoutInVoice(player, form);
            return;
        }
        const auto rightID = IntegratedMagic::Slots::GetSlotSpell(slot, Right);
        const auto leftID = IntegratedMagic::Slots::GetSlotSpell(slot, Left);
        RE::SpellItem* rightSpell = rightID ? RE::TESForm::LookupByID<RE::SpellItem>(rightID) : nullptr;
        RE::SpellItem* leftSpell = leftID ? RE::TESForm::LookupByID<RE::SpellItem>(leftID) : nullptr;
        if (rightSpell) EquipSpellInHand(player, rightSpell, Right);
        if (leftSpell) EquipSpellInHand(player, leftSpell, Left);
        const bool wantDual = (rightSpell && leftSpell && rightID == leftID);
        auto* leftCaster = GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);
        SetCasterDual(leftCaster, wantDual);
        SetCasterDual(rightCaster, wantDual);
    }
}