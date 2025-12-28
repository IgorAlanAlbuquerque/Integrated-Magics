#include "MagicHooks.h"

#include <utility>

#include "HookUtil.hpp"
#include "MagicInput.h"
#include "MagicSelect.h"
#include "MagicState.h"
#include "PCH.h"

namespace IntegratedMagic::Hooks {
    namespace {
        struct EquipSpellHook {
            using Fn = void(RE::ActorEquipManager*, RE::Actor*, RE::SpellItem*, const RE::BGSEquipSlot*);
            static inline Fn* func{nullptr};

            static void thunk(RE::ActorEquipManager* mgr, RE::Actor* actor, RE::SpellItem* spell,
                              const RE::BGSEquipSlot* slot) {
                auto const* player = RE::PlayerCharacter::GetSingleton();
                if (const bool isPlayer = (player && actor == player); isPlayer) {
                    IntegratedMagic::MagicSelect::TrySelectSpellFromEquip(actor, spell);
                }

                func(mgr, actor, spell, slot);
            }

            static void Install() {
                constexpr auto kEquipSpell = RELOCATION_ID(37939, 38895);
                Hook::stl::write_detour<EquipSpellHook>(kEquipSpell);
            }
        };

        inline std::atomic_bool g_swallowNextSelect{false};  // NOSONAR

        struct SelectSpellImplHook {
            using Fn = void (*)(RE::ActorMagicCaster*);
            static inline Fn _orig{nullptr};

            static void thunk(RE::ActorMagicCaster* caster) {
                if (!caster) {
                    return;
                }

                if (g_swallowNextSelect.exchange(false)) {
                    _orig(caster);
                    return;
                }

                auto* actor = caster->actor;
                if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || !player || actor != player) {
                    _orig(caster);
                    return;
                }

                const auto src = static_cast<std::size_t>(std::to_underlying(caster->castingSource));
                auto& rd = actor->GetActorRuntimeData();
                if (src >= std::size(rd.selectedSpells)) {
                    _orig(caster);
                    return;
                }

                _orig(caster);
                RE::MagicItem* afterItem = rd.selectedSpells[src];
                auto* afterSpell = afterItem ? afterItem->As<RE::SpellItem>() : nullptr;

                if (afterSpell && IntegratedMagic::MagicSelect::TrySelectSpellFromEquip(actor, afterSpell)) {
                    rd.selectedSpells[src] = afterItem;
                    g_swallowNextSelect.store(true);
                    return;
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
        EquipSpellHook::Install();
        SelectSpellImplHook::Install();
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
    }
}
