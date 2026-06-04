#pragma once

#include "PCH.h"

namespace IntegratedMagic {

    [[nodiscard]] bool IsChargeComplete(RE::ActorMagicCaster const* caster, RE::SpellItem const* spell);
    [[nodiscard]] bool CasterSpellMismatch(RE::ActorMagicCaster* caster, RE::SpellItem const* expected);
}
