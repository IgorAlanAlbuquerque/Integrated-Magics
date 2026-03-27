#include "HotkeyCache.h"

#include <algorithm>
#include <ranges>

#include "Config/Config.h"
#include "PCH.h"

namespace Input::detail {

    void LoadHotkeyCache_FromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const auto n = static_cast<int>(cfg.SlotCount());
        g_slotCount.store(n, std::memory_order_relaxed);

        for (auto& s : g_cache) {
            s.kb = {-1, -1, -1};
            s.gp = {-1, -1, -1};
        }

        auto fill = [](SlotHotkeys& out, const auto& in) {
            out.kb[0] = in.KeyboardScanCode1.load(std::memory_order_relaxed);
            out.kb[1] = in.KeyboardScanCode2.load(std::memory_order_relaxed);
            out.kb[2] = in.KeyboardScanCode3.load(std::memory_order_relaxed);
            out.gp[0] = in.GamepadButton1.load(std::memory_order_relaxed);
            out.gp[1] = in.GamepadButton2.load(std::memory_order_relaxed);
            out.gp[2] = in.GamepadButton3.load(std::memory_order_relaxed);
        };

        const int m = std::min(n, kMaxSlots);
        for (int i = 0; i < m; ++i) {
            const auto s = static_cast<std::size_t>(i);
            fill(g_cache[s], cfg.slotInput[s]);
            const auto& hk = g_cache[s];
            const auto kbKeys = std::ranges::count_if(hk.kb, [](int c) { return c != -1; });
            const auto gpKeys = std::ranges::count_if(hk.gp, [](int c) { return c != -1; });
            g_slotIsKbMultiKey[s] = (kbKeys > 1);
            g_slotIsGpMultiKey[s] = (gpKeys > 1);
            g_slotIsMultiKey[s] = (kbKeys > 1) || (gpKeys > 1);
#ifdef DEBUG
            spdlog::info("[Input] LoadHotkeyCache: slot={} kb=[{},{},{}] gp=[{},{},{}] isMultiKey={}", i, hk.kb[0],
                         hk.kb[1], hk.kb[2], hk.gp[0], hk.gp[1], hk.gp[2], g_slotIsMultiKey[s]);
#endif
        }

        g_hudCache = {};
        fill(g_hudCache, cfg.hudPopupInput);
    }

    bool SlotComboDown(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return false;
        const auto& hk = g_cache[static_cast<std::size_t>(slot)];
        return ComboDown(hk.kb, g_kbDown) || ComboDown(hk.gp, g_gpDown);
    }

}