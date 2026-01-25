#include "MagicHooks.h"

#include <utility>

#include "HookUtil.hpp"
#include "MagicInput.h"
#include "MagicSelect.h"
#include "MagicState.h"
#include "PCH.h"

namespace IntegratedMagic::Hooks {
    namespace {
        std::optional<IntegratedMagic::MagicSlots::Hand> ToHand(RE::MagicSystem::CastingSource src) {
            switch (src) {
                using enum RE::MagicSystem::CastingSource;
                case kLeftHand:
                    return IntegratedMagic::MagicSlots::Hand::Left;
                case kRightHand:
                    return IntegratedMagic::MagicSlots::Hand::Right;
                default:
                    return std::nullopt;
            }
        }

        struct SelectSpellImplHook {
            using Fn = void (*)(RE::ActorMagicCaster*);
            static inline Fn _orig{nullptr};

            static void thunk(RE::ActorMagicCaster* caster) {
                if (!caster) {
                    return;
                }

                auto* actor = caster->actor;
                if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || !player || actor != player) {
                    _orig(caster);
                    return;
                }

                const auto src = static_cast<std::size_t>(std::to_underlying(caster->castingSource));
                auto const& rd = actor->GetActorRuntimeData();
                if (src >= std::size(rd.selectedSpells)) {
                    _orig(caster);
                    return;
                }

                _orig(caster);
                RE::MagicItem* afterItem = rd.selectedSpells[src];

                if (auto* afterSpell = afterItem ? afterItem->As<RE::SpellItem>() : nullptr; afterSpell) {
                    const auto handOpt = ToHand(caster->castingSource);
                    if (handOpt.has_value()) {
                        (void)IntegratedMagic::MagicSelect::TrySelectSpellFromEquip(afterSpell, *handOpt);
                    }
                }
            }

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ActorMagicCaster[0]};
                const std::uintptr_t orig = vtbl.write_vfunc(0x11, thunk);
                _orig = reinterpret_cast<Fn>(orig);  // NOSONAR
            }
        };

        struct PollInputDevicesHook {
            using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
            static inline std::uintptr_t func{0};

            static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
                RE::InputEvent* headBefore = a_events ? *a_events : nullptr;
                RE::InputEvent* headAfter = IntegratedMagic::detail::FlushSyntheticInput(headBefore);

                if (func == 0) {
                    return;
                }

                RE::InputEvent* const arr[2]{headAfter, nullptr};  // NOSONAR
                auto* original = reinterpret_cast<Fn*>(func);      // NOSONAR
                original(a_dispatcher, arr);
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
                    MagicInput::HandleAnimEvent(a_ev, a_src);
                }
                return ret;
            }

            static void Install() {
                REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};
                const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);
                _orig = reinterpret_cast<Fn>(orig);  // NOSONAR - interop
            }
        };
    }

    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        SelectSpellImplHook::Install();
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
    }
}