#include "Domain/CasterUtil.h"

#include "PCH.h"

namespace IntegratedMagic {

    bool IsChargeComplete(RE::ActorMagicCaster const* caster, RE::SpellItem const* spell) {
        if (!spell) return false;
        const float charge = spell->GetChargeTime();
        if (charge <= 0.0f) return true;
        if (!caster) return false;

        const auto st = caster->state.get();
        if (st == RE::MagicCaster::State::kReady) return true;
        if (st == RE::MagicCaster::State::kCharging) return (caster->castingTimer + 1e-3f) >= charge;
        return false;
    }

    bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem const* expected) {
        if (!caster || !expected) return false;
        auto const* cur = caster->currentSpell ? caster->currentSpell->As<RE::SpellItem>() : nullptr;
        if (!cur) return false;
        return cur != expected;
    }
}
