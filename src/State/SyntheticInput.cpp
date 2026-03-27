#include "SyntheticInput.h"

#include <mutex>
#include <queue>

#include "PCH.h"

namespace IntegratedMagic::detail {
    static const RE::BSFixedString kRightAttackEvent{"Right Attack/Block"};
    static const RE::BSFixedString kLeftAttackEvent{"Left Attack/Block"};
    static const RE::BSFixedString kShoutUserEvent{"Shout"};

    const RE::BSFixedString& RightAttackEvent() { return kRightAttackEvent; }
    const RE::BSFixedString& LeftAttackEvent() { return kLeftAttackEvent; }

    struct SyntheticInputState {
        std::mutex mutex;
        std::queue<RE::ButtonEvent*> pending;
    };

    static SyntheticInputState& GetSynth() {
        static SyntheticInputState s;
        return s;
    }

    static SyntheticInputState& GetRetain() {
        static SyntheticInputState s;
        return s;
    }

    static RE::ButtonEvent* MakeAttackButtonEvent(bool leftHand, float value, float heldSecs) {
        const auto& ue = leftHand ? LeftAttackEvent() : RightAttackEvent();
        const auto id = leftHand ? kLeftAttackMouseId : kRightAttackMouseId;
        return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue, id, value, heldSecs);
    }

    static RE::InputEvent* DrainQueue(SyntheticInputState& st, RE::InputEvent* head) {
        std::queue<RE::ButtonEvent*> local;
        {
            std::scoped_lock lk(st.mutex);
            local.swap(st.pending);
        }
        if (local.empty()) return head;

        RE::InputEvent* synthHead = nullptr;
        RE::InputEvent* synthTail = nullptr;
        while (!local.empty()) {
            auto* ev = local.front();
            local.pop();
            if (!ev) continue;
            ev->next = nullptr;
            if (!synthHead) {
                synthHead = synthTail = ev;
            } else {
                synthTail->next = ev;
                synthTail = ev;
            }
        }
        if (!head) return synthHead;
        synthTail->next = head;
        return synthHead;
    }

    void EnqueueSyntheticAttack(RE::ButtonEvent* ev) {
        if (!ev) return;
        auto& st = GetSynth();
        std::scoped_lock lk(st.mutex);
        st.pending.push(ev);
    }

    void EnqueueRetainedEvent(RE::INPUT_DEVICE dev, std::uint32_t idCode, const RE::BSFixedString& userEvent,
                              float value, float heldSecs) {
        auto* ev = RE::ButtonEvent::Create(dev, userEvent, idCode, value, heldSecs);
        if (!ev) return;
        auto& st = GetRetain();
        std::scoped_lock lk(st.mutex);
        st.pending.push(ev);
    }

    RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head) {
        head = DrainQueue(GetSynth(), head);
        head = DrainQueue(GetRetain(), head);
        return head;
    }

    void DispatchAttack(Slots::Hand hand, float value, float heldSecs) {
        const bool left = (hand == Slots::Hand::Left);
        EnqueueSyntheticAttack(MakeAttackButtonEvent(left, value, heldSecs));
    }

    void DispatchShout(float value, float heldSecs) {
        auto* ev = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, kShoutUserEvent, 0, value, heldSecs);
        if (ev) EnqueueSyntheticAttack(ev);
    }
}