#include "AnimListener.h"

#include "Config/Slots.h"
#include "PCH.h"
#include "State.h"

void AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev,
                                   RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) return;

    auto* actor = ev->holder->As<RE::Actor>();
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || actor != player) return;

    using Hand = IntegratedMagic::Slots::Hand;
    auto& state = IntegratedMagic::MagicState::Get();
    const std::string_view tag{ev->tag.c_str(), ev->tag.size()};
#ifdef DEBUG
    spdlog::info("[AnimListener] Event received: tag='{}' | state.active={}", ev->tag.c_str(), state.IsActive());
#endif

    if (tag == "EnableBumper"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> EnableBumper -> NotifyAttackEnabled");
#endif
        state.NotifyAttackEnabled();
    }
    if (tag == "CastStop"sv || tag == "RitualSpellOut"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> CastStop -> OnCastStop");
#endif
        state.OnCastStop();
    }
    if (tag == "InterruptCast"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> InterruptCast -> OnCastInterrupt");
#endif
        state.OnCastInterrupt();
    }
    if (tag == "BeginCastRight"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> BeginCastRight -> OnBeginCast(Right)");
#endif
        state.OnBeginCast(Hand::Right);
    } else if (tag == "BeginCastLeft"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> BeginCastLeft -> OnBeginCast(Left)");
#endif
        state.OnBeginCast(Hand::Left);
    }
    if (tag == "shoutStop"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> shoutStop -> OnShoutStop");
#endif
        state.OnShoutStop();
    }
    if (tag == "blockStart"sv || tag == "BashExit"sv) {
#ifdef DEBUG
        spdlog::info("[AnimListener] >> {} -> ForceExit!", ev->tag.c_str());
#endif
        state.ForceExit();
    }
    if (tag == "tailMTIdle"sv || tag == "IdleStop"sv) {
        if (state.IsWaitingSheatheRestore()) {
            state.NotifySheatheComplete();
#ifdef DEBUG
            spdlog::info("[AnimListener] >> {} -> NotifySheatheComplete!");
#endif
        }
    }
    if (tag == "MRh_SpellFire_Event"sv) {
        state.OnSpellFired(Hand::Right);
    }
    if (tag == "MLh_SpellFire_Event"sv) {
        state.OnSpellFired(Hand::Left);
    }
}