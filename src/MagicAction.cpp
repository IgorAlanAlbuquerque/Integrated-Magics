#include "MagicAction.h"

#include "MagicEquipSlots.h"

namespace IntegratedMagic::MagicAction {
    namespace {
        inline void SetCasterSpell(RE::ActorMagicCaster* caster, RE::MagicItem* spell, bool select) {
            if (!caster) {
                return;
            }

            caster->SetCurrentSpellImpl(spell);

            if (!select) {
                return;
            }

            if (spell) {
                caster->SelectSpellImpl();
            } else {
                caster->DeselectSpellImpl();
            }
        }

        inline void SetCasterDual(RE::ActorMagicCaster* caster, bool dual) {
            if (caster) {
                caster->SetDualCasting(dual);
            }
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
        if (!caster || !caster->currentSpell) {
            return nullptr;
        }
        return caster->currentSpell->As<RE::SpellItem>();
    }

    void ClearHandSpell(RE::PlayerCharacter* player, EquipHand hand) {
        if (!player) {
            return;
        }

        IntegratedMagic::MagicSelect::ScopedSuppressSelection suppress{};

        auto* leftCaster = GetCaster(player, RE::MagicSystem::CastingSource::kLeftHand);
        auto* rightCaster = GetCaster(player, RE::MagicSystem::CastingSource::kRightHand);

        SetCasterDual(leftCaster, false);
        SetCasterDual(rightCaster, false);

        auto deselect = [&](RE::ActorMagicCaster* caster) {
            auto* curSpell = GetEquippedSpellFromCaster(caster);
            if (curSpell) {
                player->DeselectSpell(curSpell);
            }
        };

        switch (hand) {
            using enum IntegratedMagic::EquipHand;
            case Left:
                deselect(leftCaster);
                break;
            case Right:
                deselect(rightCaster);
                break;
            case Both:
            default:
                deselect(leftCaster);
                deselect(rightCaster);
                break;
        }
    }
}
