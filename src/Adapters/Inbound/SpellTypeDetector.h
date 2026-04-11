#pragma once
#include "Config/SpellType.h"
#include "PCH.h"

namespace IntegratedMagic::Adapters {
    [[nodiscard]] SpellType DetectSpellType(const RE::TESForm* form);
}