#include "Shared/CaptureState.h"

CaptureState& CaptureState::Get() {
    static CaptureState inst;
    return inst;
}
