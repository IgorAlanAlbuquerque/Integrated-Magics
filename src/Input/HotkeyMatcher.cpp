#include "Input/HotkeyMatcher.h"

#include <algorithm>
#include <ranges>

#include "Config/ConfigAdapter.h"
#include "PCH.h"

namespace Input::detail {

    void LoadHotkeyCache_FromConfig(HotkeyCacheStore& cache, SlotEdgeStore& slots) {
        const auto& bindings = IntegratedMagic::Config::MagicConfigAdapter::Get();
        const auto n = bindings.SlotCount();
        slots.slotCount.store(n, std::memory_order_relaxed);

        for (auto& s : cache.slots) {
            s.kb = {-1, -1, -1};
            s.gp = {-1, -1, -1};
        }

        const int m = std::min(n, kInputMaxSlots);
        for (int i = 0; i < m; ++i) {
            const auto s = static_cast<std::size_t>(i);
            const auto binding = bindings.GetSlotBinding(i);
            cache.slots[s].kb = binding.kb;
            cache.slots[s].gp = binding.gp;

            const auto& hk = cache.slots[s];
            const auto kbKeys = std::ranges::count_if(hk.kb, [](int c) { return c != -1; });
            const auto gpKeys = std::ranges::count_if(hk.gp, [](int c) { return c != -1; });
            slots.slotIsKbMultiKey[s] = (kbKeys > 1);
            slots.slotIsGpMultiKey[s] = (gpKeys > 1);
            slots.slotIsMultiKey[s] = (kbKeys > 1) || (gpKeys > 1);
            MAGIC_DEBUG_LOG("[Input] LoadHotkeyCache: slot={} kb=[{},{},{}] gp=[{},{},{}] isMultiKey={}", i, hk.kb[0],
                            hk.kb[1], hk.kb[2], hk.gp[0], hk.gp[1], hk.gp[2], slots.slotIsMultiKey[s]);
        }

        const auto hudBinding = bindings.GetHudToggleBinding();
        cache.hud.kb = hudBinding.kb;
        cache.hud.gp = hudBinding.gp;
    }

    bool SlotComboDown(int slot, const HotkeyCacheStore& cache, const KeyStateStore& keys, const SlotEdgeStore& slots) {
        if (slot < 0 || slot >= slots.ActiveSlots()) return false;
        const auto& hk = cache.slots[static_cast<std::size_t>(slot)];
        return ComboDown(hk.kb, keys.kbDown) || ComboDown(hk.gp, keys.gpDown);
    }
}