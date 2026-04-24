#include "Domain/SlotCostUtil.h"

#include "Domain/InventoryUtil.h"
#include "PCH.h"
#include "Persistence/Slots.h"
#include "Shared/Hand.h"

namespace IntegratedMagic {

    SlotAffordability ComputeSlotAffordability(int slotIndex) {
        if (Slots::IsShoutSlot(slotIndex)) return {};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return {};

        const auto rID = Slots::GetSlotSpell(slotIndex, Hand::Right);
        const auto lID = Slots::GetSlotSpell(slotIndex, Hand::Left);
        auto* rSp = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
        auto* lSp = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;

        if (!rSp && !lSp) return {};

        SlotAffordability out;
        out.hasSpells = true;
        out.available = GetPlayerMagicka(player);

        const bool hasBoth = rSp && lSp;

        if (hasBoth) {
            if (rID == lID) {
                const float cost = GetSpellMagickaCost(player, rSp);
                const float mult = GetDualCastCostMultiplier(player, rSp);
                out.totalCost = (mult > 2.f) ? cost * mult : cost * 2.f;
            } else {
                out.totalCost = GetSpellMagickaCost(player, rSp) + GetSpellMagickaCost(player, lSp);
            }
        } else {
            auto* sp = rSp ? rSp : lSp;
            out.totalCost = GetSpellMagickaCost(player, sp);
        }

        out.canCast = (out.totalCost <= 0.f) || ((out.available + 1e-2f) >= out.totalCost);
        return out;
    }
}