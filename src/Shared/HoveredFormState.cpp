#include "Shared/HoveredFormState.h"

#include <atomic>

namespace IntegratedMagic::HoveredForm {
    namespace {
        std::atomic<RE::FormID> g_formID{0};
        std::atomic<MagicType> g_type{MagicType::None};
    }

    RE::FormID GetHoveredFormID() { return g_formID.load(std::memory_order_relaxed); }
    MagicType GetHoveredMagicType() { return g_type.load(std::memory_order_relaxed); }

    void SetHoveredFormState(RE::FormID formID, MagicType type) {
        g_formID.store(formID, std::memory_order_relaxed);
        g_type.store(type, std::memory_order_relaxed);
    }
}
