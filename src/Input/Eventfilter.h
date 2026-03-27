#pragma once

#include "InputState.h"
#include "RE/I/InputEvent.h"

namespace Input::detail {

    [[nodiscard]] bool IsInputBlockedByMenus();

    void ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture);

    void FilterMouseForPopup(RE::InputEvent** a_evns);

    void FilterEvents(RE::InputEvent** a_evns);

    void UpdateSlotsIfAllowed(bool blocked, float dt);

    void DispatchIfAllowed(bool blocked, float dt);

}