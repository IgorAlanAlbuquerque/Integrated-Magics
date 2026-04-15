#pragma once
#include "PCH.h"
#include "Shared/SpellType.h"

namespace IntegratedMagic::Adapters {
    [[nodiscard]] SpellType DetectSpellType(const RE::TESForm* form);
}