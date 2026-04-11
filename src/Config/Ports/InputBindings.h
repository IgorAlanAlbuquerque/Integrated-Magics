#pragma once
#include <array>
#include <cstdint>

namespace IntegratedMagic::Config {

    struct SlotBinding {
        std::array<int, 3> kb{-1, -1, -1};
        std::array<int, 3> gp{-1, -1, -1};
    };

    class IInputBindings {
    public:
        virtual ~IInputBindings() = default;

        [[nodiscard]] virtual int SlotCount() const = 0;
        [[nodiscard]] virtual SlotBinding GetSlotBinding(int slot) const = 0;
        [[nodiscard]] virtual SlotBinding GetHudToggleBinding() const = 0;
        [[nodiscard]] virtual int ModifierKbPosition() const = 0;
        [[nodiscard]] virtual int ModifierGpPosition() const = 0;
        [[nodiscard]] virtual bool RequireExclusiveHotkey() const = 0;
        [[nodiscard]] virtual bool PressBothAtSame() const = 0;
    };
}