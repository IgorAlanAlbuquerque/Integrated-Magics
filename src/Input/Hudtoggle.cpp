#include "Input/HudToggle.h"

#include "PCH.h"

std::atomic_bool g_hudTogglePending{false};

namespace Input::detail {

    bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode, const HotkeyCacheStore& cache) {
        if (!IsHudToggleCombo(dev, convertedCode, cache)) return false;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        return ui->IsMenuOpen(magicMenu);
    }

    void UpdateHudToggleState(const HotkeyCacheStore& cache, const KeyStateStore& keys) {
        static bool prevHudDown = false;
        const bool hudDown = ComboDown(cache.hud.kb, keys.kbDown) || ComboDown(cache.hud.gp, keys.gpDown);
        if (hudDown && !prevHudDown) {
            auto* ui = RE::UI::GetSingleton();
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            if (ui && ui->IsMenuOpen(magicMenu)) g_hudTogglePending.store(true, std::memory_order_relaxed);
        }
        prevHudDown = hudDown;
    }
}