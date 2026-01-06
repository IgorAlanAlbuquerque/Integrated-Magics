#include "MagicAction.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "MagicConfig.h"
#include "MagicEquipSlots.h"

namespace IntegratedMagic::MagicAction {
    namespace {
        inline void SetCasterDual(RE::ActorMagicCaster* caster, bool dual) {
            if (caster) {
                caster->SetDualCasting(dual);
            }
        }

        void UnEquipSpell(RE::PlayerCharacter* pc, RE::SpellItem* spell, int hand) {
            auto aeMan = RE::ActorEquipManager::GetSingleton();
            if (!aeMan || !pc || !spell) {
                return;
            }

            using func_t = void (RE::ActorEquipManager::*)(RE::Actor*, RE::SpellItem*, int);
            REL::Relocation<func_t> func{RELOCATION_ID(37947, 38903)};
            return func(aeMan, pc, spell, hand);
        }

        static const RE::BSFixedString kVarSkipEquip("SkipEquipAnimation");
        static const RE::BSFixedString kVarLoadDelay("LoadBoundObjectDelay");
        static const RE::BSFixedString kVarSkip3D("Skip3DLoading");

        inline void SetSkipEquipVars(RE::PlayerCharacter* pc, bool enable, int loadDelayMs = 0, bool skip3D = false) {
            if (!pc) {
                return;
            }
            (void)pc->SetGraphVariableBool(kVarSkipEquip, enable);

            if (enable) {
                (void)pc->SetGraphVariableInt(kVarLoadDelay, loadDelayMs);
                (void)pc->SetGraphVariableBool(kVarSkip3D, skip3D);
            }
        }

        inline std::atomic<std::uint64_t> g_skipToken{0};

        inline void ScheduleDisableSkipEquip(std::uint64_t token, int delayMs) {
            std::jthread([token, delayMs]() {
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
        if (!mc) {
            return nullptr;
        }

        return skyrim_cast<RE::ActorMagicCaster*>(mc);
    }

    void EquipSpellInHand(RE::PlayerCharacter* player, RE::SpellItem* spell, EquipHand hand) {
        if (!player || !spell) {
            return;
        }

        IntegratedMagic::MagicSelect::ScopedSuppressSelection suppress{};
        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            return;
        }

        auto* leftCaster = GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        SetCasterDual(leftCaster, false);
        SetCasterDual(rightCaster, false);

        const auto* leftSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(EquipHand::Left);
        const auto* rightSlot = IntegratedMagic::EquipUtil::GetHandEquipSlot(EquipHand::Right);

        auto const& cfg = IntegratedMagic::GetMagicConfig();
        std::uint64_t token = 0;

        if (cfg.skipEquipAnimationPatch) {
            token = g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1;
            SetSkipEquipVars(player, true, /*LoadBoundObjectDelay*/ 0, /*Skip3DLoading*/ false);
            ScheduleDisableSkipEquip(token, /*delayMs*/ 1000);
        }

        switch (hand) {
            using enum IntegratedMagic::EquipHand;
            case Left:
                mgr->EquipSpell(player, spell, leftSlot);
                break;
            case Right:
                mgr->EquipSpell(player, spell, rightSlot);
                break;
            case Both:
            default:
                mgr->EquipSpell(player, spell, leftSlot);
                mgr->EquipSpell(player, spell, rightSlot);
                SetCasterDual(leftCaster, true);
                SetCasterDual(rightCaster, true);
                break;
        }
    }

    static RE::SpellItem* GetEquippedSpellFromCaster(RE::ActorMagicCaster* caster) {
        if (!caster) {
            return nullptr;
        }
        if (!caster->currentSpell) {
            return nullptr;
        }
        auto* sp = caster->currentSpell->As<RE::SpellItem>();

        return sp;
    }

    void ClearHandSpell(RE::PlayerCharacter* player, RE::SpellItem* spell, EquipHand hand) {
        if (!player) {
            return;
        }
        if (!spell) {
            return;
        }

        IntegratedMagic::MagicSelect::ScopedSuppressSelection suppress{};

        auto* leftCaster = GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        SetCasterDual(leftCaster, false);
        SetCasterDual(rightCaster, false);

        switch (hand) {
            using enum IntegratedMagic::EquipHand;
            case Left:
                UnEquipSpell(player, spell, 0);
                break;
            case Right:
                UnEquipSpell(player, spell, 1);
                break;
            default:
                break;
        }
    }

    void ClearHandSpell(RE::PlayerCharacter* player, EquipHand hand) {
        if (!player) {
            return;
        }

        auto* caster = (hand == EquipHand::Left) ? GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand)
                                                 : GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        auto* cur = GetEquippedSpellFromCaster(caster);
        if (!cur) {
            return;
        }

        ClearHandSpell(player, cur, hand);
    }
}
