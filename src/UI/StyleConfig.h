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

    enum class ButtonIconType : std::uint8_t {
        Keyboard = 0,
        PlayStation = 1,
        Xbox = 2,
    };

    enum class ButtonLabelVisibility : std::uint8_t {
        Never = 0,
        Always = 1,
        OnModifier = 2,
    };

    enum class ButtonLabelCorner : std::uint8_t {
        Top = 0,
        Right = 1,
        Bottom = 2,
        Left = 3,
        TowardCenter = 4,
        AwayFromCenter = 5,
    };

    enum class ModifierWidgetVisibility : std::uint8_t {
        Never = 0,
        Always = 1,
        HideOnPress = 2,
    };

    struct FontConfig {
        std::string path = "";
        float size = 28.f;

        bool rangePolish = false;
        bool rangeCyrillic = false;
        bool rangeJapanese = false;
        bool rangeChineseSimplified = false;
        bool rangeKorean = false;
        bool rangeGreek = false;
    };

    struct SlotShapeVertex {
        float x = 0.f;
        float y = 0.f;
    };

    struct SlotShapeConfig {
        std::vector<SlotShapeVertex> vertices;
        bool useCustomShape = false;

        void SetCircle(int segments = 16);
        void SetSquare();
        void SetDiamond();
        void SetStar(int points = 5, float innerFactor = 0.45f);
    };

    struct StyleConfig {
        FontConfig font;
        SlotShapeConfig slotShape;
        float slotRadius = 32.f;
        float ringRadius = 54.f;
        float popupSlotRadius = 48.f;
        float popupRingRadius = 90.f;
        float popupSlotGap = 24.f;
        HudLayoutType popupLayout = HudLayoutType::Circular;
        float modeWidgetW = 58.f;
        float popupOffsetX = 0.f;
        float popupOffsetY = 0.f;
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

        bool useTextureForSlotBg = false;

        ButtonIconType buttonIconType = ButtonIconType::Xbox;

        ModifierWidgetVisibility modifierWidgetVisibility = ModifierWidgetVisibility::Always;
        float modifierWidgetRadius = 14.f;
        float modifierWidgetOffsetX = 0.f;
        float modifierWidgetOffsetY = 0.f;

        ButtonLabelVisibility buttonLabelVisibility = ButtonLabelVisibility::OnModifier;
        ButtonLabelCorner buttonLabelCorner = ButtonLabelCorner::AwayFromCenter;
        float buttonLabelIconSize = 20.f;
        float buttonLabelIconSpacing = 2.f;
        float buttonLabelMargin = 4.f;
        float buttonLabelOffsetX = 0.f;
        float buttonLabelOffsetY = 0.f;
        float buttonLabelFadeTime = 0.12f;

        std::uint32_t slotBgActive = 0xE60F161Eu;
        std::uint32_t slotBgInactive = 0xC80C0C0Cu;

        std::uint32_t slotRingInactive = 0x96464646u;
        std::uint8_t slotRingActiveAlpha = 245;
        float slotRingWidth = 2.5f;

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
        void Save();

        static StyleConfig& Get() {
            static StyleConfig inst;
            return inst;
        }

    private:
        StyleConfig() = default;
    };
}