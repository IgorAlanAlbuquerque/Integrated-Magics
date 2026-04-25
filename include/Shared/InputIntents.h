#pragma once
#include <atomic>
#include <mutex>
#include <vector>

namespace Input::detail {

    enum class PopupInputKind {
        MouseDelta,
        StickDelta,
        Click,
        RightClick,
        Close,
    };

    struct PopupInputEvent {
        PopupInputKind kind;
        float dx;
        float dy;
    };

    inline std::vector<PopupInputEvent> g_pendingPopupInputs;
    inline std::mutex g_popupInputsMtx;

    inline void PushPopupInput(PopupInputEvent e) {
        std::scoped_lock _{g_popupInputsMtx};
        g_pendingPopupInputs.push_back(e);
    }

    inline std::vector<PopupInputEvent> DrainPopupInputs() {
        std::scoped_lock _{g_popupInputsMtx};
        std::vector<PopupInputEvent> out;
        out.swap(g_pendingPopupInputs);
        return out;
    }

    inline std::atomic_bool g_popupOpenForInput{false};
}