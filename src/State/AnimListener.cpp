#include "AnimListener.h"

#include "PCH.h"
#include "Persistence/Slots.h"
#include "State.h"

void AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev,
                                   RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) return;

    auto* actor = ev->holder->As<RE::Actor>();
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || actor != player) return;

    using Hand = IntegratedMagic::Slots::Hand;
    auto& state = IntegratedMagic::MagicState::Get();
    const std::string_view tag{ev->tag.c_str(), ev->tag.size()};

    if (tag == "EnableBumper"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> EnableBumper -> NotifyAttackEnabled");

        state.NotifyAttackEnabled();
    }
    if (tag == "CastStop"sv || tag == "RitualSpellOut"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> CastStop -> OnCastStop");

        state.OnCastStop();
    }
    if (tag == "InterruptCast"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> InterruptCast -> OnCastInterrupt");

        state.OnCastInterrupt();
    }
    if (tag == "BeginCastRight"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> BeginCastRight -> OnBeginCast(Right)");

        state.OnBeginCast(Hand::Right);
    } else if (tag == "BeginCastLeft"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> BeginCastLeft -> OnBeginCast(Left)");

        state.OnBeginCast(Hand::Left);
    }
    if (tag == "shoutStop"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> shoutStop -> OnShoutStop");

        state.OnShoutStop();
    }
    if (tag == "blockStart"sv || tag == "BashExit"sv) {
        MAGIC_DEBUG_LOG("[AnimListener] >> {} -> ForceExit!", ev->tag.c_str());

        if (!state.IsPressMode()) {
            state.ForceExit();
        }
    }
    if (tag == "tailMTIdle"sv || tag == "IdleStop"sv) {
        if (state.IsWaitingSheatheRestore()) {
            state.NotifySheatheComplete();

            MAGIC_DEBUG_LOG("[AnimListener] >> {} -> NotifySheatheComplete!");
        }
    }
    if (tag == "MRh_SpellFire_Event"sv) {
        state.OnSpellFired(Hand::Right);
    }
    if (tag == "MLh_SpellFire_Event"sv) {
        state.OnSpellFired(Hand::Left);
    }
}