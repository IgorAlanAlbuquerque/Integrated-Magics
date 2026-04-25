#include "Hooks.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <xinput.h>

#include <utility>

#include "Adapters/Inbound/AnimEventAdapter.h"
#include "Adapters/Inbound/D3DInitHook.h"
#include "Adapters/Inbound/DXGIPresentHook.h"
#include "Adapters/Inbound/MagicCasterInterruptHook.h"
#include "Adapters/Inbound/MagicCasterStartCastHook.h"
#include "Adapters/Inbound/PlayerAnimGraphHook.h"
#include "Adapters/Inbound/PollInputDevicesHook.h"

namespace IntegratedMagic::Hooks {
    void Install_Hooks() {
        Inbound::PollInputDevicesHook::Install();
        Inbound::PlayerAnimGraphHook::Install();
        Inbound::D3DInitHook::Install();
        Inbound::DXGIPresentHook::Install();
        Inbound::MagicCasterInterruptHook::Install();
        Inbound::MagicCasterStartCastHook::Install();
    }
}