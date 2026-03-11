#include "Hooks.h"

#include <utility>

#include "HookUtil.hpp"
#include "Input/Input.h"
#include "PCH.h"
#include "State/AnimListener.h"
#include "State/State.h"

namespace IntegratedMagic::Hooks {
    namespace {
        struct PollInputDevicesHook {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                if (!a_events) return;

                Input::ProcessAndFilter(const_cast<RE::InputEvent**>(a_events));

                RE::InputEvent* head = IntegratedMagic::detail::FlushSyntheticInput(*a_events);

                if (func == 0) return;
                RE::InputEvent* const arr[2]{head, nullptr};
                reinterpret_cast<Fn*>(func)(a_dispatcher, arr);
            }

            static void Install() {
                Hook::stl::write_call<PollInputDevicesHook>(REL::RelocationID(67315, 68617),
                                                            REL::VariantOffset(0x7B, 0x7B, 0x81));
            }
        };

        struct PlayerAnimGraphProcessEventHook {
            using Fn = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
                                                    const RE::BSAnimationGraphEvent*,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
            static inline Fn _orig{nullptr};

            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                  const RE::BSAnimationGraphEvent* a_ev,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
                const auto ret = _orig ? _orig(a_this, a_ev, a_src) : RE::BSEventNotifyControl::kContinue;
                if (a_ev) {
                    AnimListener::HandleAnimEvent(a_ev, a_src);
                }
                return ret;
            }

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};
                const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);
                _orig = reinterpret_cast<Fn>(orig);
            }
        };
    }

    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
    }
}