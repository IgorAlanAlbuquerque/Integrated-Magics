
#pragma once
#include <cstdint>
#include <vector>

#include "Shared/SpellType.h"

namespace IntegratedMagic::Config {

    struct SpellTypeDefaults {
        ActivationMode mode;
        bool autoAttack;
    };

    class ISlotAssignments {
    public:
        virtual ~ISlotAssignments() = default;

        [[nodiscard]] virtual int SlotCount() const = 0;
        [[nodiscard]] virtual std::uint32_t GetSpell(int slot, bool leftHand) const = 0;
        [[nodiscard]] virtual std::uint32_t GetShout(int slot) const = 0;
        [[nodiscard]] virtual SpellTypeDefaults GetSpellDefaults(SpellType t) const = 0;

        virtual void SetSpell(int slot, bool leftHand, std::uint32_t formID) = 0;
        virtual void SetShout(int slot, std::uint32_t formID) = 0;
        virtual void ClearSlots() = 0;

        virtual void ReadAllSlots(std::vector<std::uint32_t>& outLeft, std::vector<std::uint32_t>& outRight,
                                  std::vector<std::uint32_t>& outShout) const = 0;

        virtual void ApplyAllSlots(const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right,
                                   const std::vector<std::uint32_t>& shout) = 0;

        virtual void Save() = 0;
    };
}