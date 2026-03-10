#pragma once

#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic {

    namespace detail {
        constexpr std::uint32_t kRightAttackMouseId = 0;
        constexpr std::uint32_t kLeftAttackMouseId = 1;
        inline const RE::BSFixedString kRightAttackEvent{"Right Attack/Block"};
        inline const RE::BSFixedString kLeftAttackEvent{"Left Attack/Block"};
        inline const RE::BSFixedString& RightAttackEvent() { return kRightAttackEvent; }
        inline const RE::BSFixedString& LeftAttackEvent() { return kLeftAttackEvent; }

        struct SyntheticInputState {
            std::mutex mutex;
            std::queue<RE::ButtonEvent*> pending;
        };

        void EnqueueSyntheticAttack(RE::ButtonEvent* ev);

        void EnqueueRetainedEvent(RE::INPUT_DEVICE dev, std::uint32_t idCode, const RE::BSFixedString& userEvent,
                                  float value, float heldSecs);

        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);

        void DispatchAttack(IntegratedMagic::Slots::Hand hand, float value, float heldSecs);
        void DispatchShout(float value, float heldSecs);
    }

    struct SpellSettings;

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
        RE::FormID snapShoutID{0};
        bool valid{false};
    };

    struct InventoryIndex {
        std::unordered_map<RE::TESBoundObject*, std::vector<RE::ExtraDataList*>> extrasByBase;
        std::unordered_set<RE::TESBoundObject*> wornBases;
    };

    InventoryIndex BuildInventoryIndex(RE::PlayerCharacter* player);

    struct HandMode {
        IntegratedMagic::ActivationMode mode{IntegratedMagic::ActivationMode::Hold};
        bool wantAutoAttack{true};
        bool pressActive{false};
        bool holdActive{false};
        bool autoActive{false};
        bool waitingChargeComplete{false};
        bool chargeComplete{false};
        bool waitingAutoAfterEquip{false};
        bool holdFiredAndWaitingCastStop{false};
        bool finished{false};
        float waitingEnableBumperSecs{0.0f};
        bool waitingBeginCast{false};
        float beginCastWaitSecs{0.f};
        int beginCastRetries{0};
    };

    class MagicState {
    public:
        static MagicState& Get();
        struct ExtraEquippedItem {
            RE::TESBoundObject* base{nullptr};
            RE::ExtraDataList* extra{nullptr};
        };
        void OnSlotPressed(int slot);
        void OnSlotReleased(int slot);
        void OnCastStop();
        void OnCastInterrupt();
        void OnStaggerStop();
        void TryFinalizeExit();
        bool IsActive() const noexcept { return _active; }
        int ActiveSlot() const noexcept { return _activeSlot; }
        void StartAutoAttack(IntegratedMagic::Slots::Hand hand);
        void StopAutoAttack(IntegratedMagic::Slots::Hand hand);
        void StopAllAutoAttack();
        void PumpAutoAttack(float dt);
        void NotifyAttackEnabled();
        void PumpAutomatic(float dt);
        void OnBeginCast(IntegratedMagic::Slots::Hand hand);
        void OnShoutStop();
        const HandMode& LeftMode() const noexcept { return _left; }
        const HandMode& RightMode() const noexcept { return _right; }

    private:
        MagicState() = default;
        struct DelayedStart {
            bool pending{false};
            float secs{0.f};
        };

        struct SlotEntry {
            RE::PlayerCharacter* player{nullptr};
            std::uint32_t leftID{0};
            std::uint32_t rightID{0};
            RE::SpellItem* leftSpell{nullptr};
            RE::SpellItem* rightSpell{nullptr};
            SpellSettings leftSettings{};
            SpellSettings rightSettings{};
            bool hasLeft{false};
            bool hasRight{false};

            bool isShout{false};
            std::uint32_t shoutID{0};
            RE::TESForm* shoutForm{nullptr};
            SpellSettings shoutSettings{};
        };

        DelayedStart _delayStartLeft{};
        DelayedStart _delayStartRight{};

        static constexpr float kDelayedStartSec = 0.050f;

        DelayedStart& DelayFor(IntegratedMagic::Slots::Hand hand) {
            using enum IntegratedMagic::Slots::Hand;
            return (hand == Left) ? _delayStartLeft : _delayStartRight;
        }

        void ScheduleDelayedStart(IntegratedMagic::Slots::Hand hand) {
            auto& d = DelayFor(hand);
            d.pending = true;
            d.secs = 0.f;
        }

        void CancelDelayedStart(IntegratedMagic::Slots::Hand hand) {
            auto& d = DelayFor(hand);
            d.pending = false;
            d.secs = 0.f;
        }

        void CancelAllDelayedStarts() {
            _delayStartLeft = {};
            _delayStartRight = {};
        }

        void PumpDelayedStarts(float dt);
        static inline RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }
        HandMode& ModeFor(IntegratedMagic::Slots::Hand hand) noexcept {
            return (hand == IntegratedMagic::Slots::Hand::Left) ? _left : _right;
        }
        const HandMode& ModeFor(IntegratedMagic::Slots::Hand hand) const noexcept {
            return (hand == IntegratedMagic::Slots::Hand::Left) ? _left : _right;
        }
        void EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot);
        void CaptureSnapshot(RE::PlayerCharacter const* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);
        bool HandIsRelevant(IntegratedMagic::Slots::Hand h) const;
        bool AllRelevantHandsFinished() const;
        void ExitAllNow();
        bool CanOverwriteNow() const;
        void PrepareForOverwriteToSlot(int newSlot);
        void PumpAutoStartFallback(IntegratedMagic::Slots::Hand hand, float dt);
        void DisableHand(IntegratedMagic::Slots::Hand hand);
        bool PrepareSlotEntry(int slot, SlotEntry& out);
        void EnterHand(IntegratedMagic::Slots::Hand hand, const SpellSettings& ss);
        void TogglePressHand(IntegratedMagic::Slots::Hand hand, const SpellSettings& ss);
        void FinishHand(IntegratedMagic::Slots::Hand hand);
        void PumpAutomaticHand(IntegratedMagic::Slots::Hand hand);
        void SetModeSpellsFromHand(IntegratedMagic::Slots::Hand hand, RE::SpellItem* spell);
        void StartShoutPress();
        void StopShoutPress();
        static inline bool IsLeft(IntegratedMagic::Slots::Hand h) { return h == IntegratedMagic::Slots::Hand::Left; }
        void MarkDirty(IntegratedMagic::Slots::Hand h) {
            if (IsLeft(h))
                _dirtyLeft = true;
            else
                _dirtyRight = true;
        }
        template <class Fn>
        void UpdatePrevExtraEquippedForOverlay(Fn&& equipFn);

        std::vector<ExtraEquippedItem> _prevExtraEquipped;
        HandMode _left{};
        HandMode _right{};
        bool _active{false};
        int _activeSlot{-1};
        bool _aaHeldLeft{false};
        bool _aaHeldRight{false};
        float _aaSecsLeft{0.f};
        float _aaSecsRight{0.f};
        bool _isDualCasting{false};
        bool _attackEnabled{false};
        HandSnapshot _snap{};
        RE::SpellItem* _modeSpellLeft{nullptr};
        RE::SpellItem* _modeSpellRight{nullptr};
        bool _dirtyLeft{false};
        bool _dirtyRight{false};
        int _firstInterrupt = 0;
        bool _pendingRestore{false};
        bool _pendingSkipFirstCastStop{false};

        std::uint32_t _modeShoutID{0};
        bool _shoutFinished{false};
        bool _dirtyShout{false};
        bool _shoutHeld{false};
        float _shoutHeldSecs{0.f};
        bool _shoutIsPower{false};
        float _powerAutoSecs{0.f};
        bool _shoutWaitingStopEvent{false};
    };

    template <class Fn>
    void MagicState::UpdatePrevExtraEquippedForOverlay(Fn&& equipFn) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        auto before = BuildInventoryIndex(player);
        std::forward<Fn>(equipFn)();
        auto after = BuildInventoryIndex(player);
        for (auto* base : before.wornBases) {
            if (after.wornBases.contains(base)) {
                continue;
            }
            ExtraEquippedItem item{base, nullptr};
            bool exists = false;
            for (auto const& e : _prevExtraEquipped) {
                if (e.base == item.base) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                _prevExtraEquipped.push_back(item);
            }
        }
    }
}