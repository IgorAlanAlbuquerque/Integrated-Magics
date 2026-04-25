#include "Adapters/Inbound/PlayerAnimGraphHook.h"

#include "Adapters/Inbound/AnimEventAdapter.h"
#include "PCH.h"

namespace IntegratedMagic::Inbound::PlayerAnimGraphHook {
    namespace {
        struct Impl {
            using Fn = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
                                                    const RE::BSAnimationGraphEvent*,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
            static inline Fn _orig{nullptr};

            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                  const RE::BSAnimationGraphEvent* a_ev,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
                const auto ret = _orig ? _orig(a_this, a_ev, a_src) : RE::BSEventNotifyControl::kContinue;
                if (a_ev) {
                    AnimListener::HandleAnimEvent(a_ev);
                }
                return ret;
            }
        };
    }

    void Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};
        const std::uintptr_t orig = vtbl.write_vfunc(1, &Impl::thunk);
        Impl::_orig = reinterpret_cast<Impl::Fn>(orig);
    }
}