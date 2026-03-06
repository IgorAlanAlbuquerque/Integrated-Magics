#include "AnimListener.h"

#include "Config/Slots.h"
#include "State.h"
#include "PCH.h"

void AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev,
                                   RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) return;

    auto* actor = ev->holder->As<RE::Actor>();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!actor || actor != player) return;

    using Hand = IntegratedMagic::Slots::Hand;
    auto& state = IntegratedMagic::MagicState::Get();
    const std::string_view tag{ev->tag.c_str(), ev->tag.size()};

    if (tag == "EnableBumper"sv) {
        state.NotifyAttackEnabled();
        state.OnStaggerStop();
    }
    if (tag == "CastStop"sv) {
        state.OnCastStop();
    }
    if (tag == "InterruptCast"sv) {
        state.OnCastInterrupt();
    }
    if (tag == "BeginCastRight"sv) {
        state.OnBeginCast(Hand::Right);
    } else if (tag == "BeginCastLeft"sv) {
        state.OnBeginCast(Hand::Left);
    }
}