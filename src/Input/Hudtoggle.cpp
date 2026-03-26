#include "HudToggle.h"

#include "PCH.h"

namespace Input::detail {

    namespace {

        [[nodiscard]] bool IsHudComboDown() {
            return ComboDown(g_hudCache.kb, g_kbDown) || ComboDown(g_hudCache.gp, g_gpDown);
        }

    }

    bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode) {
        if (!IsHudToggleCombo(dev, convertedCode)) return false;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        return ui->IsMenuOpen(magicMenu);
    }

    void UpdateHudToggleState() {
        static bool prevHudDown = false;
        const bool hudDown = IsHudComboDown();

        if (hudDown && !prevHudDown) {
            auto* ui = RE::UI::GetSingleton();
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            if (ui && ui->IsMenuOpen(magicMenu)) {
                g_hudTogglePending.store(true, std::memory_order_relaxed);
            }
        }

        prevHudDown = hudDown;
    }

}