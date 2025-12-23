#include "MagicHooks.h"

#include "HookUtil.hpp"
#include "MagicSelect.h"
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

                const auto src = static_cast<std::size_t>(caster->castingSource);
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
                _orig = reinterpret_cast<Fn>(orig);
            }
        };
    }

    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        EquipSpellHook::Install();
        SelectSpellImplHook::Install();
    }
}
