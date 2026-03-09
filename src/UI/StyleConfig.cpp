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
        modeWidgetW = GetFloat(ini, "Popup", "ModeWidgetWidth", modeWidgetW);
        iconSizeFactor = GetFloat(ini, "Icons", "SizeFactor", iconSizeFactor);
        iconOffsetFactor = GetFloat(ini, "Icons", "OffsetFactor", iconOffsetFactor);
        overlayAlpha = GetU8(ini, "Popup", "OverlayAlpha", overlayAlpha);

        slotBgActive = GetColor(ini, "Colors", "SlotBgActive", slotBgActive);
        slotBgInactive = GetColor(ini, "Colors", "SlotBgInactive", slotBgInactive);
        slotRingInactive = GetColor(ini, "Colors", "SlotRingInactive", slotRingInactive);
        slotRingActiveAlpha = GetU8(ini, "Colors", "SlotRingActiveAlpha", slotRingActiveAlpha);
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

}