#include "HudController.h"

#include "Application/InputController.h"

namespace Application {

    HudController& HudController::Get() {
        static HudController inst;
        return inst;
    }

    bool HudController::ConsumeHudToggle() { return InputController::Get().ConsumeHudToggle(); }

    bool HudController::IsModifierHeld() const { return InputController::Get().IsModifierHeld(); }

}