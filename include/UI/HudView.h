#pragma once

#include <array>
#include <cstdint>

namespace RE {
    class SpellItem;
    class TESForm;
}

namespace IntegratedMagic::HUD {

    inline constexpr int kMaxViewSlots = 64;

    enum class HoveredMagicType : int {
        None = 0,
        Spell,
        TwoHandedSpell,
        RightOnlySpell,
        LeftOnlySpell,
        Shout,
        Power,
    };

    struct SlotView {
        RE::SpellItem const* rightSpell{};
        RE::SpellItem const* leftSpell{};
        std::uint32_t rightSpellID{};
        std::uint32_t leftSpellID{};
        std::uint32_t shoutFormID{};
        RE::TESForm const* labelForm{};
        bool isTwoHanded{};

        bool hasSpells{};
        bool canCast{};
        bool onCooldown{};
        bool justFinishedCooldown{};
        float cooldownProgress{};

        std::array<int, 3> kbCodes{-1, -1, -1};
        std::array<int, 3> gpCodes{-1, -1, -1};
    };

    struct HudView {
        int slotCount{};
        int activeSlot{-1};
        bool spellSystemActive{};
        bool modifierHeld{};

        int modifierKbPos{};
        int modifierGpPos{};
        int modifierKbCode{-1};
        int modifierGpCode{-1};

        HoveredMagicType hoveredMagicType{HoveredMagicType::None};

        std::array<SlotView, kMaxViewSlots> slots{};
    };
}