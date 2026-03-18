#pragma once
#include <cstdint>

namespace IntegratedMagic {

    enum class HudLayoutType : std::uint8_t {
        Circular = 0,
        Horizontal = 1,
        Vertical = 2,
        Grid = 3,
    };

    enum class HudAnchor : std::uint8_t {
        TopLeft = 0,
        TopCenter = 1,
        TopRight = 2,
        MiddleLeft = 3,
        Center = 4,
        MiddleRight = 5,
        BottomLeft = 6,
        BottomCenter = 7,
        BottomRight = 8,
    };

    struct StyleConfig {
        float slotRadius = 32.f;
        float ringRadius = 54.f;
        float popupSlotRadius = 48.f;
        float popupRingRadius = 90.f;
        float popupSlotGap = 24.f;
        float modeWidgetW = 58.f;
        float iconSizeFactor = 0.90f;
        float iconOffsetFactor = 0.28f;
        std::uint8_t overlayAlpha = 160;

        float slotActiveScale = 1.35f;
        float slotModifierScale = 1.20f;
        float slotNeighborScale = 0.88f;
        float slotExpandTime = 0.12f;
        float slotRetractTime = 0.15f;

        HudAnchor hudAnchor = HudAnchor::BottomRight;
        float hudOffsetX = 0.f;
        float hudOffsetY = 0.f;

        HudLayoutType hudLayout = HudLayoutType::Circular;
        float slotSpacing = 8.f;
        int gridColumns = 2;

        std::uint32_t slotBgActive = 0xE60F161Eu;
        std::uint32_t slotBgInactive = 0xC80C0C0Cu;

        std::uint32_t slotRingInactive = 0x96464646u;

        std::uint8_t slotRingActiveAlpha = 245;

        std::uint8_t iconAlpha = 220;

        std::uint32_t emptySlotColor = 0x6E5A5A5Au;

        std::uint32_t ringCenterFill = 0x96464646u;
        std::uint32_t ringCenterBorder = 0xAA6E6E6Eu;

        std::uint32_t alterationFill = 0xC8FFB464u;
        std::uint32_t alterationGlow = 0x46FFB464u;
        std::uint32_t conjurationFill = 0xC8F064A0u;
        std::uint32_t conjurationGlow = 0x46F064A0u;
        std::uint32_t destructionFill = 0xC8374BFFu;
        std::uint32_t destructionGlow = 0x46374BFFu;
        std::uint32_t illusionFill = 0xC823AFE6u;
        std::uint32_t illusionGlow = 0x4623AFE6u;
        std::uint32_t restorationFill = 0xC878D746u;
        std::uint32_t restorationGlow = 0x4678D746u;
        std::uint32_t defaultFill = 0xA0646464u;
        std::uint32_t defaultGlow = 0x00000000u;
        std::uint32_t emptyFill = 0x78282828u;

        void Load();

        static StyleConfig& Get() {
            static StyleConfig inst;
            return inst;
        }

    private:
        StyleConfig() = default;
    };
}