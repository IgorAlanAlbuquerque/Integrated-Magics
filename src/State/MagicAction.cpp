#include "MagicAction.h"

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
            return;
        }
        ClearHandSpell(player, cur, hand);
    }
}