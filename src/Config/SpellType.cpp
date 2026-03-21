#include "SpellType.h"

#include "PCH.h"

namespace IntegratedMagic {
    SpellType DetectSpellType(const RE::TESForm* form) {
        if (!form) return SpellType::Unknown;

        if (form->Is(RE::FormType::Shout)) return SpellType::Shout;

        const auto* spell = form->As<RE::MagicItem>();
        if (!spell) return SpellType::Unknown;

        if (const auto* si = form->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            if (si->GetSpellType() == ST::kPower || si->GetSpellType() == ST::kLesserPower) return SpellType::Power;
        }

        using CT = RE::MagicSystem::CastingType;
        if (spell->GetCastingType() == CT::kConcentration) return SpellType::Concentration;

        for (const auto& entry : spell->effects) {
            if (!entry || !entry->baseEffect) continue;
            using Arch = RE::EffectArchetypes::ArchetypeID;
            if (entry->baseEffect->GetArchetype() == Arch::kBoundWeapon) return SpellType::Bound;
        }

        return SpellType::Cast;
    }
}