#pragma once

namespace RE {
    struct BSAnimationGraphEvent;
    template <class T>
    class BSTEventSource;
}

namespace AnimListener {
    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>* src);
}