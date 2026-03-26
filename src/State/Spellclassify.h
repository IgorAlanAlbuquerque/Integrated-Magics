#pragma once

#include "PCH.h"

namespace IntegratedMagic::SpellClassify {
    inline constexpr RE::FormID kRightHandSlotID = 0x00013F42u;
    inline constexpr RE::FormID kLeftHandSlotID = 0x00013F43u;
    inline constexpr RE::FormID kEitherHandSlotID = 0x00013F44u;
    inline constexpr RE::FormID kBothHandsSlotID = 0x00013F45u;

    [[nodiscard]] inline RE::FormID GetSpellEquipSlotID(const RE::SpellItem* spell) {
        if (!spell) return 0;
        const auto* slot = spell->GetEquipSlot();
        return slot ? slot->GetFormID() : 0;
    }

    [[nodiscard]] inline bool IsTwoHandedSpell(const RE::SpellItem* spell) {
        if (!spell) return false;
        using ST = RE::MagicSystem::SpellType;
        const auto t = spell->GetSpellType();
        if (t == ST::kPower || t == ST::kLesserPower || t == ST::kVoicePower) return false;
        return GetSpellEquipSlotID(spell) == kBothHandsSlotID;
    }

    [[nodiscard]] inline bool IsRightHandOnlySpell(const RE::SpellItem* spell) {
        return spell && GetSpellEquipSlotID(spell) == kRightHandSlotID;
    }

    [[nodiscard]] inline bool IsLeftHandOnlySpell(const RE::SpellItem* spell) {
        return spell && GetSpellEquipSlotID(spell) == kLeftHandSlotID;
    }
}