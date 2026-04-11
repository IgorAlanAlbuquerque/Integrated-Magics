
#pragma once
#include <vector>

#include "Config/Config.h"
#include "Config/Ports/HudSettings.h"
#include "Config/Ports/InputBindings.h"
#include "Config/Ports/PatchSettings.h"
#include "Config/Ports/SlotAssignments.h"
#include "Persistence/SaveSpellDB.h"

namespace IntegratedMagic::Config {

    class MagicConfigAdapter final : public ISlotAssignments,
                                     public IInputBindings,
                                     public IHudSettings,
                                     public IPatchSettings {
    public:
        static MagicConfigAdapter& Get();

        int SlotCount() const override;
        std::uint32_t GetSpell(int slot, bool leftHand) const override;
        std::uint32_t GetShout(int slot) const override;
        SpellTypeDefaults GetSpellDefaults(SpellType t) const override;
        void SetSpell(int slot, bool leftHand, std::uint32_t formID) override;
        void SetShout(int slot, std::uint32_t formID) override;
        void ClearSlots() override;
        void ReadAllSlots(std::vector<std::uint32_t>& outLeft, std::vector<std::uint32_t>& outRight,
                          std::vector<std::uint32_t>& outShout) const override;
        void ApplyAllSlots(const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right,
                           const std::vector<std::uint32_t>& shout) override;
        void Save() override;

        SlotBinding GetSlotBinding(int slot) const override;
        SlotBinding GetHudToggleBinding() const override;
        int ModifierKbPosition() const override;
        int ModifierGpPosition() const override;
        bool RequireExclusiveHotkey() const override;
        bool PressBothAtSame() const override;

        bool FlagSet(HudVisibilityFlag f) const override;

        bool SkipEquipAnimation() const override;
        bool SkipEquipAnimationOnReturn() const override;

    private:
        MagicConfigAdapter() = default;
        [[nodiscard]] MagicConfig& Cfg() const;

        SaveSpellSlots m_currentSlots{};
    };
}