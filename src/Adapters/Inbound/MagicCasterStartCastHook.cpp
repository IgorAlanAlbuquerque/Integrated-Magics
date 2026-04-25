#include "Adapters/Inbound/MagicCasterStartCastHook.h"

#include "Application/SpellSystemController.h"
#include "PCH.h"

namespace IntegratedMagic::Inbound::MagicCasterStartCastHook {
    namespace {
        struct Impl {
            using Fn = void(RE::MagicCaster*);
            static inline Fn* _orig{nullptr};

            static void thunk(RE::MagicCaster* self) {
                if (_orig) _orig(self);

                auto* actor = self->GetCasterAsActor();
                if (!actor || !actor->IsPlayerRef()) return;

                const auto src = self->GetCastingSource();
                auto* spell = self->currentSpell;
                const auto kind = spell ? spell->GetCastingType() : RE::MagicSystem::CastingType::kFireAndForget;

                Application::SpellSystemController::Get().OnCastStarted(src, spell, kind);
            }
        };
    }

    void Install() {
        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ActorMagicCaster[0]};
        Impl::_orig = reinterpret_cast<Impl::Fn*>(vtbl.write_vfunc(6, &Impl::thunk));
    }
}