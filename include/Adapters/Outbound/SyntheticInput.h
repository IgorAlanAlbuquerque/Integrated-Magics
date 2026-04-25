#pragma once

#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::detail {
    constexpr std::uint32_t kRightAttackMouseId = 0;
    constexpr std::uint32_t kLeftAttackMouseId = 1;

    const RE::BSFixedString& RightAttackEvent();
    const RE::BSFixedString& LeftAttackEvent();

    void EnqueueRetainedEvent(RE::INPUT_DEVICE dev, std::uint32_t idCode, const RE::BSFixedString& userEvent,
                              float value, float heldSecs);

    RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);

    void DispatchAttack(Hand hand, float value, float heldSecs);
    void DispatchShout(float value, float heldSecs);
}