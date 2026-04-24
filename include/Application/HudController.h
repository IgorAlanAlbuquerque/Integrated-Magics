#pragma once
#include "PCH.h"

namespace Application {

    class HudController {
    public:
        static HudController& Get();

        [[nodiscard]] bool ConsumeHudToggle();
        [[nodiscard]] bool IsModifierHeld() const;

    private:
        HudController() = default;
    };

}