#pragma once
#include <mutex>
#include <vector>

namespace IntegratedMagic::HUD {

    enum class SlotIntentKind {
        AssignHovered,
        ClearSlot,
        ClosePopup,
    };

    struct SlotIntent {
        SlotIntentKind kind;
        int slot;
        bool hoverRight;
    };

    inline std::vector<SlotIntent> g_pendingIntents;
    inline std::mutex g_intentsMtx;

    inline void PushIntent(SlotIntent i) {
        std::scoped_lock _{g_intentsMtx};
        g_pendingIntents.push_back(i);
    }

    inline std::vector<SlotIntent> DrainIntents() {
        std::scoped_lock _{g_intentsMtx};
        std::vector<SlotIntent> out;
        out.swap(g_pendingIntents);
        return out;
    }
}