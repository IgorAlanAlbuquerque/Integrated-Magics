#include "Adapters/Inbound/MagicCasterInterruptHook.h"

#include "Application/SpellSystemController.h"
#include "PCH.h"

namespace IntegratedMagic::Inbound::MagicCasterInterruptHook {
    namespace {
        struct Impl {
            using Fn = void(RE::MagicCaster*, bool);
            static inline Fn* _orig{nullptr};

            static void thunk(RE::MagicCaster* self, bool a_depleteEnergy) {
                auto* actor = self->GetCasterAsActor();
                const bool isPlayer = actor && actor->IsPlayerRef();
                const auto src = self->GetCastingSource();
                auto* spell = self->currentSpell;

                if (_orig) _orig(self, a_depleteEnergy);

                if (isPlayer) {
                    Application::SpellSystemController::Get().OnCastInterrupted(src, spell, a_depleteEnergy);
                }
            }
        };
    }

    void Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ActorMagicCaster[0]};
        Impl::_orig = reinterpret_cast<Impl::Fn*>(vtbl.write_vfunc(8, &Impl::thunk));
    }
}