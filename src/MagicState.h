#pragma once
#include "PCH.h"

namespace IntegratedMagic {
    namespace detail {
        void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);
        void DispatchAttack(EquipHand hand, float value, float heldSecs);
    }
    struct ObjSnapshot {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
        RE::FormID formID{0};
    };

    struct HandSnapshot {
        ObjSnapshot rightObj{};
        ObjSnapshot leftObj{};
        RE::MagicItem* rightSpell{nullptr};
        RE::MagicItem* leftSpell{nullptr};
        bool valid{false};
    };

    class MagicState {
    public:
        static MagicState& Get();

        struct ExtraEquippedItem {
            RE::TESBoundObject* base{nullptr};
            RE::ExtraDataList* extra{nullptr};
        };

        std::vector<ExtraEquippedItem> _prevExtraEquipped;

        void TogglePress(int slot);
        bool IsActive() const;
        int ActiveSlot() const;
        void HoldDown(int slot);
        void HoldUp(int slot);
        bool IsHoldActive() const { return _holdActive; }
        int HoldSlot() const { return _holdSlot; }
        void StartAutoAttack(EquipHand hand);
        void StopAutoAttack();
        void PumpAutoAttack(float dt);
        void NotifyAttackEnabled();
        void RequestAutoAttackStart(EquipHand hand);
        void TryStartWaitingAutoAttack();

    private:
        void EnterPress(int slot);
        bool ApplyPress(int slot);
        void EnterHold(int slot);
        void ExitAll();

        void CaptureSnapshot(RE::PlayerCharacter* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);
        void UpdatePrevExtraEquippedForOverlay(const std::function<void()>& equipFn);

        bool _active{false};
        int _activeSlot{-1};
        bool _holdActive{false};
        int _holdSlot{-1};
        HandSnapshot _snap{};
        bool _autoAttackHeld{false};
        EquipHand _autoAttackHand{EquipHand::Right};
        float _autoAttackSecs{0.0f};
        bool _attackEnabled = false;
        bool _waitingAutoAfterEquip = false;
        EquipHand _waitingAutoHand = EquipHand::Right;
    };
}
