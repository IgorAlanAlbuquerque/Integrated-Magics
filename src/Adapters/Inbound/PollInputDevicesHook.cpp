#include "Adapters/Inbound/PollInputDevicesHook.h"

#include "Adapters/Outbound/SyntheticInput.h"
#include "Application/InputController.h"
#include "Application/SpellSystemController.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace IntegratedMagic::Inbound::PollInputDevicesHook {
    namespace {
        struct Impl {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                if (!a_events) return;

                Application::InputController::Get().ProcessAndFilter(const_cast<RE::InputEvent**>(a_events));
                const float dt = Application::InputController::Get().GetDeltaTime();
                const bool blocked = Application::InputController::Get().IsInputBlocked();
                Application::SpellSystemController::Get().OnFrame(dt, blocked);
                RE::InputEvent* head = IntegratedMagic::detail::FlushSyntheticInput(*a_events);

                if (func == 0) return;
                RE::InputEvent* const arr[2]{head, nullptr};
                reinterpret_cast<Fn*>(func)(a_dispatcher, arr);
            }
        };
    }

    void Install() {
        Hook::stl::write_call<Impl>(REL::RelocationID(67315, 68617, 0xC519E0), REL::VariantOffset(0x7B, 0x7B, 0x81));
    }
}