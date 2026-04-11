#pragma once
#include "PCH.h"

namespace Application {

    class HudController {
    public:
        static HudController& Get();

        // Consultado pelo HudManager no DrawHudFrame
        [[nodiscard]] bool ConsumeHudToggle();

        // Consultado pelo SlotDrawer
        [[nodiscard]] bool IsModifierHeld() const;

    private:
        HudController() = default;
    };

}