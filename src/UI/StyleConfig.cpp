#include "StyleConfig.h"

#include "PCH.h"
#include "SimpleIni.h"

namespace IntegratedMagic {
    namespace {
        float GetFloat(const CSimpleIniA& ini, const char* section, const char* key, float def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            try {
                return std::stof(v);
            } catch (...) {
                return def;
            }
        }

        std::uint8_t GetU8(const CSimpleIniA& ini, const char* section, const char* key, std::uint8_t def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            try {
                return static_cast<std::uint8_t>(std::stoul(v));
            } catch (...) {
                return def;
            }
        }

        std::uint32_t GetColor(const CSimpleIniA& ini, const char* section, const char* key, std::uint32_t def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            try {
                return static_cast<std::uint32_t>(std::stoul(v, nullptr, 0));
            } catch (...) {
                return def;
            }
        }

        HudAnchor GetAnchor(const CSimpleIniA& ini, const char* section, const char* key, HudAnchor def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            const std::string s{v};
            if (s == "TopLeft" || s == "0") return HudAnchor::TopLeft;
            if (s == "TopCenter" || s == "1") return HudAnchor::TopCenter;
            if (s == "TopRight" || s == "2") return HudAnchor::TopRight;
            if (s == "MiddleLeft" || s == "3") return HudAnchor::MiddleLeft;
            if (s == "Center" || s == "4") return HudAnchor::Center;
            if (s == "MiddleRight" || s == "5") return HudAnchor::MiddleRight;
            if (s == "BottomLeft" || s == "6") return HudAnchor::BottomLeft;
            if (s == "BottomCenter" || s == "7") return HudAnchor::BottomCenter;
            if (s == "BottomRight" || s == "8") return HudAnchor::BottomRight;
            return def;
        }

        HudLayoutType GetLayout(const CSimpleIniA& ini, const char* section, const char* key, HudLayoutType def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            const std::string s{v};
            if (s == "Circular" || s == "0") return HudLayoutType::Circular;
            if (s == "Horizontal" || s == "1") return HudLayoutType::Horizontal;
            if (s == "Vertical" || s == "2") return HudLayoutType::Vertical;
            if (s == "Grid" || s == "3") return HudLayoutType::Grid;
            return def;
        }

        ButtonIconType GetButtonIconType(const CSimpleIniA& ini, const char* section, const char* key,
                                         ButtonIconType def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            const std::string s{v};
            if (s == "Keyboard" || s == "0") return ButtonIconType::Keyboard;
            if (s == "PlayStation" || s == "1") return ButtonIconType::PlayStation;
            if (s == "Xbox" || s == "2") return ButtonIconType::Xbox;
            return def;
        }

        ButtonLabelVisibility GetButtonLabelVisibility(const CSimpleIniA& ini, const char* section, const char* key,
                                                       ButtonLabelVisibility def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            const std::string s{v};
            if (s == "Never" || s == "0") return ButtonLabelVisibility::Never;
            if (s == "Always" || s == "1") return ButtonLabelVisibility::Always;
            if (s == "OnModifier" || s == "2") return ButtonLabelVisibility::OnModifier;
            return def;
        }

        ButtonLabelCorner GetButtonLabelCorner(const CSimpleIniA& ini, const char* section, const char* key,
                                               ButtonLabelCorner def) {
            const char* v = ini.GetValue(section, key, nullptr);
            if (!v) return def;
            const std::string s{v};
            if (s == "Top" || s == "0") return ButtonLabelCorner::Top;
            if (s == "Right" || s == "1") return ButtonLabelCorner::Right;
            if (s == "Bottom" || s == "2") return ButtonLabelCorner::Bottom;
            if (s == "Left" || s == "3") return ButtonLabelCorner::Left;
            if (s == "TowardCenter" || s == "4") return ButtonLabelCorner::TowardCenter;
            if (s == "AwayFromCenter" || s == "5") return ButtonLabelCorner::AwayFromCenter;
            return def;
        }
    }

    void StyleConfig::Load() {
        constexpr const char* kPath = R"(.\Data\SKSE\Plugins\IntegratedMagics\styles.ini)";

        CSimpleIniA ini;
        ini.SetUnicode();
        const SI_Error rc = ini.LoadFile(kPath);
        if (rc < 0) {
            spdlog::info("[StyleConfig] styles.ini não encontrado — usando defaults.");
            return;
        }

        slotRadius = GetFloat(ini, "HUD", "SlotRadius", slotRadius);
        ringRadius = GetFloat(ini, "HUD", "RingRadius", ringRadius);
        popupSlotRadius = GetFloat(ini, "Popup", "SlotRadius", popupSlotRadius);
        popupRingRadius = GetFloat(ini, "Popup", "RingRadius", popupRingRadius);
        popupSlotGap = GetFloat(ini, "Popup", "SlotGap", popupSlotGap);
        popupLayout = GetLayout(ini, "Popup", "Layout", popupLayout);
        modeWidgetW = GetFloat(ini, "Popup", "ModeWidgetWidth", modeWidgetW);
        iconSizeFactor = GetFloat(ini, "Icons", "SizeFactor", iconSizeFactor);
        iconOffsetFactor = GetFloat(ini, "Icons", "OffsetFactor", iconOffsetFactor);
        overlayAlpha = GetU8(ini, "Popup", "OverlayAlpha", overlayAlpha);

        slotActiveScale = GetFloat(ini, "HUD", "SlotActiveScale", slotActiveScale);
        slotModifierScale = GetFloat(ini, "HUD", "SlotModifierScale", slotModifierScale);
        slotNeighborScale = GetFloat(ini, "HUD", "SlotNeighborScale", slotNeighborScale);
        slotExpandTime = GetFloat(ini, "HUD", "SlotExpandTime", slotExpandTime);
        slotRetractTime = GetFloat(ini, "HUD", "SlotRetractTime", slotRetractTime);
        hudAnchor = GetAnchor(ini, "HUD", "Anchor", hudAnchor);
        hudOffsetX = GetFloat(ini, "HUD", "OffsetX", hudOffsetX);
        hudOffsetY = GetFloat(ini, "HUD", "OffsetY", hudOffsetY);
        hudLayout = GetLayout(ini, "HUD", "Layout", hudLayout);
        slotSpacing = GetFloat(ini, "HUD", "SlotSpacing", slotSpacing);
        gridColumns = static_cast<int>(GetFloat(ini, "HUD", "GridColumns", static_cast<float>(gridColumns)));

        {
            const char* v = ini.GetValue("HUD", "UseTextureForSlotBg", nullptr);
            if (v) useTextureForSlotBg = (_stricmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
        }

        buttonIconType = GetButtonIconType(ini, "General", "ButtonIconType", buttonIconType);

        {
            const char* v = ini.GetValue("HUD", "ModifierWidgetVisibility", nullptr);
            if (v) {
                const std::string s{v};
                if (s == "Never" || s == "0")
                    modifierWidgetVisibility = ModifierWidgetVisibility::Never;
                else if (s == "Always" || s == "1")
                    modifierWidgetVisibility = ModifierWidgetVisibility::Always;
                else if (s == "HideOnPress" || s == "2")
                    modifierWidgetVisibility = ModifierWidgetVisibility::HideOnPress;
            }
        }
        modifierWidgetRadius = GetFloat(ini, "HUD", "ModifierWidgetRadius", modifierWidgetRadius);
        modifierWidgetOffsetX = GetFloat(ini, "HUD", "ModifierWidgetOffsetX", modifierWidgetOffsetX);
        modifierWidgetOffsetY = GetFloat(ini, "HUD", "ModifierWidgetOffsetY", modifierWidgetOffsetY);

        buttonLabelVisibility = GetButtonLabelVisibility(ini, "HUD", "ButtonLabelVisibility", buttonLabelVisibility);
        buttonLabelCorner = GetButtonLabelCorner(ini, "HUD", "ButtonLabelCorner", buttonLabelCorner);
        buttonLabelIconSize = GetFloat(ini, "HUD", "ButtonLabelIconSize", buttonLabelIconSize);
        buttonLabelIconSpacing = GetFloat(ini, "HUD", "ButtonLabelIconSpacing", buttonLabelIconSpacing);
        buttonLabelMargin = GetFloat(ini, "HUD", "ButtonLabelMargin", buttonLabelMargin);
        buttonLabelOffsetX = GetFloat(ini, "HUD", "ButtonLabelOffsetX", buttonLabelOffsetX);
        buttonLabelOffsetY = GetFloat(ini, "HUD", "ButtonLabelOffsetY", buttonLabelOffsetY);
        buttonLabelFadeTime = GetFloat(ini, "HUD", "ButtonLabelFadeTime", buttonLabelFadeTime);

        slotBgActive = GetColor(ini, "Colors", "SlotBgActive", slotBgActive);
        slotBgInactive = GetColor(ini, "Colors", "SlotBgInactive", slotBgInactive);
        slotRingInactive = GetColor(ini, "Colors", "SlotRingInactive", slotRingInactive);
        slotRingActiveAlpha = GetU8(ini, "Colors", "SlotRingActiveAlpha", slotRingActiveAlpha);
        slotRingWidth = GetFloat(ini, "Colors", "SlotRingWidth", slotRingWidth);
        iconAlpha = GetU8(ini, "Colors", "IconAlpha", iconAlpha);
        emptySlotColor = GetColor(ini, "Colors", "EmptySlotColor", emptySlotColor);

        ringCenterFill = GetColor(ini, "Colors", "RingCenterFill", ringCenterFill);
        ringCenterBorder = GetColor(ini, "Colors", "RingCenterBorder", ringCenterBorder);

        alterationFill = GetColor(ini, "SchoolColors", "AlterationFill", alterationFill);
        alterationGlow = GetColor(ini, "SchoolColors", "AlterationGlow", alterationGlow);
        conjurationFill = GetColor(ini, "SchoolColors", "ConjurationFill", conjurationFill);
        conjurationGlow = GetColor(ini, "SchoolColors", "ConjurationGlow", conjurationGlow);
        destructionFill = GetColor(ini, "SchoolColors", "DestructionFill", destructionFill);
        destructionGlow = GetColor(ini, "SchoolColors", "DestructionGlow", destructionGlow);
        illusionFill = GetColor(ini, "SchoolColors", "IllusionFill", illusionFill);
        illusionGlow = GetColor(ini, "SchoolColors", "IllusionGlow", illusionGlow);
        restorationFill = GetColor(ini, "SchoolColors", "RestorationFill", restorationFill);
        restorationGlow = GetColor(ini, "SchoolColors", "RestorationGlow", restorationGlow);
        defaultFill = GetColor(ini, "SchoolColors", "DefaultFill", defaultFill);
        defaultGlow = GetColor(ini, "SchoolColors", "DefaultGlow", defaultGlow);
        emptyFill = GetColor(ini, "SchoolColors", "EmptyFill", emptyFill);

        spdlog::info("[StyleConfig] styles.ini carregado.");
    }

    void StyleConfig::Save() {
        constexpr const char* kPath = R"(.\Data\SKSE\Plugins\IntegratedMagics\styles.ini)";

        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(kPath);

        auto setFloat = [&](const char* sec, const char* key, float v) {
            std::string s = std::to_string(v);

            if (s.find('.') != std::string::npos) {
                s.erase(s.find_last_not_of('0') + 1);
                if (s.back() == '.') s += '0';
            }
            ini.SetValue(sec, key, s.c_str());
        };
        auto setInt = [&](const char* sec, const char* key, int v) { ini.SetLongValue(sec, key, v); };
        auto setBool = [&](const char* sec, const char* key, bool v) { ini.SetBoolValue(sec, key, v); };
        auto setColor = [&](const char* sec, const char* key, std::uint32_t v) {
            char buf[12];
            std::snprintf(buf, sizeof(buf), "0x%08Xu", v);
            ini.SetValue(sec, key, buf);
        };
        auto setU8 = [&](const char* sec, const char* key, std::uint8_t v) {
            ini.SetLongValue(sec, key, static_cast<long>(v));
        };

        static const char* kAnchorNames[] = {"TopLeft",     "TopCenter",  "TopRight",     "MiddleLeft", "Center",
                                             "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight"};
        static const char* kLayoutNames[] = {"Circular", "Horizontal", "Vertical", "Grid"};
        static const char* kButtonIconTypeNames[] = {"Keyboard", "PlayStation", "Xbox"};
        static const char* kButtonLabelVisibilityNames[] = {"Never", "Always", "OnModifier"};
        static const char* kButtonLabelCornerNames[] = {"Top",  "Right",        "Bottom",
                                                        "Left", "TowardCenter", "AwayFromCenter"};

        setFloat("HUD", "SlotRadius", slotRadius);
        setFloat("HUD", "RingRadius", ringRadius);
        setFloat("HUD", "SlotActiveScale", slotActiveScale);
        setFloat("HUD", "SlotModifierScale", slotModifierScale);
        setFloat("HUD", "SlotNeighborScale", slotNeighborScale);
        setFloat("HUD", "SlotExpandTime", slotExpandTime);
        setFloat("HUD", "SlotRetractTime", slotRetractTime);
        ini.SetValue("HUD", "Anchor", kAnchorNames[static_cast<int>(hudAnchor)]);
        setFloat("HUD", "OffsetX", hudOffsetX);
        setFloat("HUD", "OffsetY", hudOffsetY);
        ini.SetValue("HUD", "Layout", kLayoutNames[static_cast<int>(hudLayout)]);
        setFloat("HUD", "SlotSpacing", slotSpacing);
        setInt("HUD", "GridColumns", gridColumns);
        setBool("HUD", "UseTextureForSlotBg", useTextureForSlotBg);

        ini.SetValue("General", "ButtonIconType", kButtonIconTypeNames[static_cast<int>(buttonIconType)]);

        static const char* kModifierWidgetVisibilityNames[] = {"Never", "Always", "HideOnPress"};
        ini.SetValue("HUD", "ModifierWidgetVisibility",
                     kModifierWidgetVisibilityNames[static_cast<int>(modifierWidgetVisibility)]);
        setFloat("HUD", "ModifierWidgetRadius", modifierWidgetRadius);
        setFloat("HUD", "ModifierWidgetOffsetX", modifierWidgetOffsetX);
        setFloat("HUD", "ModifierWidgetOffsetY", modifierWidgetOffsetY);

        ini.SetValue("HUD", "ButtonLabelVisibility",
                     kButtonLabelVisibilityNames[static_cast<int>(buttonLabelVisibility)]);
        ini.SetValue("HUD", "ButtonLabelCorner", kButtonLabelCornerNames[static_cast<int>(buttonLabelCorner)]);
        setFloat("HUD", "ButtonLabelIconSize", buttonLabelIconSize);
        setFloat("HUD", "ButtonLabelIconSpacing", buttonLabelIconSpacing);
        setFloat("HUD", "ButtonLabelMargin", buttonLabelMargin);
        setFloat("HUD", "ButtonLabelOffsetX", buttonLabelOffsetX);
        setFloat("HUD", "ButtonLabelOffsetY", buttonLabelOffsetY);
        setFloat("HUD", "ButtonLabelFadeTime", buttonLabelFadeTime);

        setFloat("Popup", "SlotRadius", popupSlotRadius);
        setFloat("Popup", "RingRadius", popupRingRadius);
        setFloat("Popup", "SlotGap", popupSlotGap);
        setFloat("Popup", "ModeWidgetWidth", modeWidgetW);
        setU8("Popup", "OverlayAlpha", overlayAlpha);

        setFloat("Icons", "SizeFactor", iconSizeFactor);
        setFloat("Icons", "OffsetFactor", iconOffsetFactor);

        setColor("Colors", "SlotBgActive", slotBgActive);
        setColor("Colors", "SlotBgInactive", slotBgInactive);
        setColor("Colors", "SlotRingInactive", slotRingInactive);
        setU8("Colors", "SlotRingActiveAlpha", slotRingActiveAlpha);
        setFloat("Colors", "SlotRingWidth", slotRingWidth);
        setU8("Colors", "IconAlpha", iconAlpha);
        setColor("Colors", "EmptySlotColor", emptySlotColor);
        setColor("Colors", "RingCenterFill", ringCenterFill);
        setColor("Colors", "RingCenterBorder", ringCenterBorder);

        setColor("SchoolColors", "AlterationFill", alterationFill);
        setColor("SchoolColors", "AlterationGlow", alterationGlow);
        setColor("SchoolColors", "ConjurationFill", conjurationFill);
        setColor("SchoolColors", "ConjurationGlow", conjurationGlow);
        setColor("SchoolColors", "DestructionFill", destructionFill);
        setColor("SchoolColors", "DestructionGlow", destructionGlow);
        setColor("SchoolColors", "IllusionFill", illusionFill);
        setColor("SchoolColors", "IllusionGlow", illusionGlow);
        setColor("SchoolColors", "RestorationFill", restorationFill);
        setColor("SchoolColors", "RestorationGlow", restorationGlow);
        setColor("SchoolColors", "DefaultFill", defaultFill);
        setColor("SchoolColors", "DefaultGlow", defaultGlow);
        setColor("SchoolColors", "EmptyFill", emptyFill);

        ini.SaveFile(kPath);
        spdlog::info("[StyleConfig] styles.ini salvo.");
    }
}