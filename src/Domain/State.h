#pragma once

#include <vector>

#include "Domain/InventoryUtil.h"
#include "Domain/OutboundDelegate.h"
#include "PCH.h"
#include "Shared/AttackEnabledResult.h"
#include "Shared/Hand.h"
#include "Shared/SlotPressAction.h"
#include "Shared/SlotPressResult.h"
#include "Shared/SpellSettings.h"
#include "Shared/SpellType.h"

namespace IntegratedMagic {
    struct SpellSettings;

    struct HandSnapshot {
        ObjSnapshot rightObj{};
        ObjSnapshot leftObj{};
        RE::MagicItem* rightSpell{nullptr};
        RE::MagicItem* leftSpell{nullptr};
        RE::FormID snapShoutID{0};
        bool valid{false};
    };

    enum class AutoCastPhase : std::uint8_t {
        Idle = 0,
        WaitingAttackEnable,   // equipou / aguardando EnableBumper ou fallback
        StartRequested,        // já apertou ataque virtual, aguardando BeginCast real
        Casting,               // cast começou de verdade
        WaitingChargeRelease,  // aguardando completar charge para soltar
        Done                   // mão finalizada
    };

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
        bool pressAutocast{false};
        float waitingEnableBumperSecs{0.0f};
        bool waitingBeginCast{false};
        float beginCastWaitSecs{0.f};
        int beginCastRetries{0};
        bool waitingSpellFireFinalize{false};
        float spellFireFinalizeSecs{0.f};
        AutoCastPhase autoCastPhase{AutoCastPhase::Idle};
        float startRequestSecs{0.f};  // tempo desde que pediu start
        float stalledCastSecs{0.f};
        bool sawBeginCastEvent{false};
    };

    struct SessionState {
        bool active{false};
        int activeSlot{-1};
        bool isDualCasting{false};
        bool attackEnabled{false};
        bool wasHandsDown{false};
        float activeTimeoutSecs{0.f};
        int firstInterrupt{0};
        int dualCastSkipCastStops{0};

        RE::SpellItem* modeSpellLeft{nullptr};
        RE::SpellItem* modeSpellRight{nullptr};

        void Reset() { *this = {}; }
    };

    struct PumpResult {
        struct AttackEvent {
            Hand hand;
            float power;
            float secsHeld;
        };
        struct ShoutEvent {
            float power;
            float secsHeld;
        };

        std::optional<AttackEvent> leftAttack;
        std::optional<AttackEvent> rightAttack;
        std::optional<ShoutEvent> shout;
    };

    struct RestoreContext {
        HandSnapshot snapshot{};
        std::vector<ExtraEquippedItem> prevExtraEquipped;
        bool dirtyLeft{false};
        bool dirtyRight{false};
        bool dirtyShout{false};
        bool pendingRestore{false};
        bool pendingRestoreAfterSheathe{false};
        bool sheatheAnimComplete{false};
        bool pendingPowerRestore{false};
        float pendingPowerRestoreDelaySecs{0.f};
        static constexpr float kPowerRestoreDelaySec = 0.05f;
        float sheatheWaitSecs{0.f};
        static constexpr float kSheatheWaitTimeoutSec = 1.0f;

        void ClearDirty() { dirtyLeft = dirtyRight = dirtyShout = false; }
        void ClearPending() { pendingRestore = pendingRestoreAfterSheathe = pendingPowerRestore = false; }
        void Reset() { *this = {}; }
    };

    struct AutoAttackState {
        bool heldLeft{false};
        bool heldRight{false};
        float secsLeft{0.f};
        float secsRight{0.f};

        bool& Held(Hand h) noexcept { return h == Hand::Left ? heldLeft : heldRight; }
        float& Secs(Hand h) noexcept { return h == Hand::Left ? secsLeft : secsRight; }

        void Reset() { *this = {}; }
    };

    struct ShoutState {
        std::uint32_t modeShoutID{0};
        bool finished{false};
        bool dirty{false};
        bool held{false};
        float heldSecs{0.f};
        bool isPower{false};
        float powerAutoSecs{0.f};
        bool waitingStopEvent{false};

        bool Active() const noexcept { return modeShoutID != 0; }
        void Reset() { *this = {}; }
    };

    struct CastFlags {
        int castStopsToSkip{0};
        void Reset() { *this = {}; }
    };

    class MagicState {
    public:
        static MagicState& Get();

        [[nodiscard]] SlotPressAction OnSlotPressed(int slot);
        void OnEquipComplete(const InventoryIndex& snapshotBefore);
        void OnSlotReleased(int slot);

        void OnBeginCast(Hand hand);
        void OnCastStop();
        void OnCastInterrupt();
        void OnShoutStop();
        void ForceExit();
        void ForceExitNoRestore();

        void PumpAutomatic(float dt);
        PumpResult PumpAutoAttack(float dt);
        void TryFinalizeExit();

        [[nodiscard]] AttackEnabledResult NotifyAttackEnabled();

        bool IsActive() const noexcept { return _session.active; }
        int ActiveSlot() const noexcept { return _session.activeSlot; }
        bool IsDualCasting() const noexcept { return _session.isDualCasting; }
        bool PendingSkipFirstCastStop() const noexcept { return _cast.castStopsToSkip > 0; }
        int DualCastSkipCount() const noexcept { return _session.dualCastSkipCastStops; }
        bool IsWaitingSheatheRestore() const noexcept {
            return _restore.pendingRestoreAfterSheathe && !_restore.sheatheAnimComplete;
        }
        bool IsPressMode() const noexcept { return _left.pressActive || _right.pressActive; }
        void NotifySheatheComplete() noexcept { _restore.sheatheAnimComplete = true; }
        void OnSpellFired(Hand hand);
        const HandMode& LeftMode() const noexcept { return _left; }
        const HandMode& RightMode() const noexcept { return _right; }
        bool IsInSlotSetup() const noexcept { return _inSlotSetup; }
        [[nodiscard]] bool IsShoutActive() const noexcept { return _shout.modeShoutID != 0; }
        void SetOutboundDelegate(const Domain::OutboundDelegate& delegate);

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

        void ResetHandStates() {
            _left = {};
            _right = {};
            _aa.Reset();
            _cast.Reset();
            _session.attackEnabled = false;
            _session.isDualCasting = false;
            _session.dualCastSkipCastStops = 0;
            _session.firstInterrupt = 0;
            _session.activeTimeoutSecs = 0.f;
            _session.modeSpellLeft = nullptr;
            _session.modeSpellRight = nullptr;
            CancelAllDelayedStarts();
        }

        void ResetShoutState() { _shout.Reset(); }

        void ResetSessionState() {
            ResetHandStates();
            ResetShoutState();
            _restore.ClearDirty();
            _session.active = false;
            _session.activeSlot = -1;
        }

        DelayedStart& DelayFor(Hand hand) noexcept { return hand == Hand::Left ? _delayStartLeft : _delayStartRight; }

        void ScheduleDelayedStart(Hand hand) {
            auto& d = DelayFor(hand);
            d.pending = true;
            d.secs = 0.f;
        }

        void CancelDelayedStart(Hand hand) {
            auto& d = DelayFor(hand);
            d.pending = false;
            d.secs = 0.f;
        }

        void CancelAllDelayedStarts() {
            _delayStartLeft = {};
            _delayStartRight = {};
        }

        static RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }

        HandMode& ModeFor(Hand hand) noexcept { return hand == Hand::Left ? _left : _right; }
        const HandMode& ModeFor(Hand hand) const noexcept { return hand == Hand::Left ? _left : _right; }

        static bool IsLeft(Hand h) noexcept { return h == Hand::Left; }

        void MarkDirty(Hand h) {
            if (IsLeft(h))
                _restore.dirtyLeft = true;
            else
                _restore.dirtyRight = true;
        }

        void EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot, bool raiseHandsIfSheathed = true);
        void CaptureSnapshot(RE::PlayerCharacter const* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);

        bool HandIsRelevant(Hand h) const;
        bool AllRelevantHandsFinished() const;
        bool CanOverwriteNow() const;
        bool ShouldForceInterrupt() const;

        void ExitAllNow();
        void PrepareForOverwriteToSlot(int newSlot);
        void DisableHand(Hand hand);

        bool PrepareSlotEntry(int slot, SlotEntry& out);
        void EnterHand(Hand hand, const SpellSettings& ss, bool skipAnim);
        void TogglePressHand(Hand hand, const SpellSettings& ss);
        void FinishHand(Hand hand);
        void SetModeSpellsFromHand(Hand hand, RE::SpellItem* spell);

        void PumpDelayedStarts(float dt);
        void PumpAutomaticHand(Hand hand);
        void PumpAutoStartFallback(Hand hand, float dt);
        void ScheduleSpellFireFinalize(Hand hand);
        void PumpSpellFireFinalize(float dt);
        bool RequestAutoAttackStart(Hand hand, bool clearWaitAfterEquip);
        void ConfirmAutoCastStarted(Hand hand);
        void ResetAutoCastStartState(Hand hand);
        bool HasRealCastStarted(Hand hand, const RE::SpellItem* expectedSpell) const;
        bool IsCasterIdleForExpectedSpell(Hand hand, const RE::SpellItem* expectedSpell) const;

        template <class Fn>
        void UpdatePrevExtraEquippedForOverlay(Fn&& equipFn);

        HandMode _left{};
        HandMode _right{};
        DelayedStart _delayStartLeft{};
        DelayedStart _delayStartRight{};

        SessionState _session{};
        RestoreContext _restore{};
        AutoAttackState _aa{};
        ShoutState _shout{};
        CastFlags _cast{};
        bool _inSlotSetup{false};
        Domain::OutboundDelegate _outbound{};

        static constexpr float kDelayedStartSec = 0.050f;
        static constexpr float kMaxActiveTimeoutSecs = 30.f;
    };

    template <class Fn>
    void MagicState::UpdatePrevExtraEquippedForOverlay(Fn&& equipFn) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto before = BuildInventoryIndex(player);
        std::forward<Fn>(equipFn)();
        auto after = BuildInventoryIndex(player);

        for (auto* base : before.wornBases) {
            if (after.wornBases.contains(base)) continue;
            const bool exists =
                std::ranges::any_of(_restore.prevExtraEquipped, [&](auto const& e) { return e.base == base; });
            if (!exists) _restore.prevExtraEquipped.push_back({base, nullptr});
        }
    }
}