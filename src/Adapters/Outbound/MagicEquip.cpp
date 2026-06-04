#include "Adapters/Outbound/MagicEquip.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "Adapters/Outbound/EquipSlots.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::MagicAction {
    namespace {
        const RE::BSFixedString kInstantAnim("InstantEquipAnim");
        inline std::atomic<std::uint64_t> g_skipToken{0};  // NOSONAR

        inline void SetCasterDual(RE::ActorMagicCaster* caster, bool dual) {
            if (caster) {
                caster->SetDualCasting(dual);
            }
        }

        RE::MagicSystem::CastingSource ToCastingSource(Hand hand) {
            return (hand == Hand::Left) ? RE::MagicSystem::CastingSource::kLeftHand
                                        : RE::MagicSystem::CastingSource::kRightHand;
        }

        const RE::BGSEquipSlot* ToEquipSlot(Hand hand) { return IntegratedMagic::EquipUtil::GetHandEquipSlot(hand); }

        int ToUnEquipHandInt(Hand hand) { return (hand == Hand::Left) ? 0 : 1; }

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

                        MAGIC_DEBUG_LOG(
                            "[Action] ScheduleDisableSkipEquip: token={} - disabling InstantEquipAnim (timer fallback)",
                            token);

                        g_skipToken.fetch_add(1, std::memory_order_relaxed);
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

    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand, bool skipAnim) {
        if (!player || !spell) return;
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) return;
        auto* caster = GetCaster(player, ToCastingSource(hand));
        SetCasterDual(caster, false);

        MAGIC_DEBUG_LOG("[Action] EquipSpellInHand: hand={} spellID={:#010x} name='{}' | currentCasterSpell={:#010x}",
                        (hand == Hand::Left) ? "Left" : "Right", spell->GetFormID(),
                        spell->GetFullName() ? spell->GetFullName() : "<null>",
                        (caster && caster->currentSpell) ? caster->currentSpell->GetFormID() : 0u);

        if (skipAnim) {
            const std::uint64_t token = (g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1) | 1uLL;
            g_skipToken.store(token, std::memory_order_relaxed);
            SetSkipEquipVars(player, true);
            MAGIC_DEBUG_LOG("[Action] EquipSpellInHand: InstantEquipAnim = true (token={})", token);
            ScheduleDisableSkipEquip(token, 500);
        }

        const auto* equipSlot = ToEquipSlot(hand);
        mgr->EquipSpell(player, spell, equipSlot);
    }

    void SetSkipEquipVars(RE::PlayerCharacter* pc, bool enable) {
        if (!pc) return;
        const std::uint64_t token = (g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1) | 1uLL;
        g_skipToken.store(token, std::memory_order_relaxed);
        (void)pc->SetGraphVariableBool(kInstantAnim, enable);
        ScheduleDisableSkipEquip(token, 500);
    }

    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand) {
        if (!player || !spell) {
            return;
        }

        MAGIC_DEBUG_LOG("[Action] ClearHandSpell(spell): hand={} spellID={:#010x}",
                        (hand == Hand::Left) ? "Left" : "Right", spell->GetFormID());

        auto* caster = GetCaster(player, ToCastingSource(hand));
        SetCasterDual(caster, false);
        UnEquipSpell(player, spell, ToUnEquipHandInt(hand));
    }

    void ClearHandSpell(RE::PlayerCharacter* player, Hand hand) {
        if (!player) {
            return;
        }
        auto* caster = GetCaster(player, ToCastingSource(hand));
        auto* cur = GetEquippedSpellFromCaster(caster);
        if (!cur) {
            MAGIC_DEBUG_LOG("[Action] ClearHandSpell(no-spell): hand={} - caster has no spell, skipping",
                            (hand == Hand::Left) ? "Left" : "Right");

            return;
        }

        MAGIC_DEBUG_LOG("[Action] ClearHandSpell(no-spell): hand={} clearing spellID={:#010x}",
                        (hand == Hand::Left) ? "Left" : "Right", cur->GetFormID());

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
            const auto& rd = player->GetActorRuntimeData();
            MAGIC_DEBUG_LOG("[Action] EquipShoutInVoice: power {:#010x} -> selectedPower={:#010x}",
                            spell->GetFormID(), rd.selectedPower ? rd.selectedPower->GetFormID() : 0u);
        }
    }

    void ClearVoiceShout(RE::PlayerCharacter* player) {
        if (!player) return;
        if (auto* shout = player->GetCurrentShout()) {
            UnEquipShout(player, shout);
            return;
        }
        auto const& rd = player->GetActorRuntimeData();
        MAGIC_DEBUG_LOG("[Action] ClearVoiceShout: selectedPower={:#010x}",
                        rd.selectedPower ? rd.selectedPower->GetFormID() : 0u);
        if (auto* power = rd.selectedPower ? rd.selectedPower->As<RE::SpellItem>() : nullptr) {
            if (IsPowerSpell(power)) {
                UnEquipSpell(player, power, 2);
                UnEquipSpell(player, power, 1);
                UnEquipSpell(player, power, 0);
            }
        }
    }

    void ApplySkipEquipAnimReturn(RE::PlayerCharacter* player, bool skipAnimOnReturn) {
        if (!skipAnimOnReturn) return;
        const std::uint64_t token = (g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1) | 1uLL;
        g_skipToken.store(token, std::memory_order_relaxed);
        SetSkipEquipVars(player, true);
        MAGIC_DEBUG_LOG("[Action] ApplySkipEquipAnimReturn: InstantEquipAnim = true (token={})", token);
        ScheduleDisableSkipEquip(token, 500);
    }

    void ResetSkipEquipToken() {
        uint64_t cur = g_skipToken.load(std::memory_order_relaxed);
        if (cur & 1uLL) {
            g_skipToken.fetch_add(1, std::memory_order_relaxed);
        }
    }
}