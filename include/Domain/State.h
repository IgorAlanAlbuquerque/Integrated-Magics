#pragma once

#include <vector>

#include "PCH.h"
#include "Shared/AttackEnabledResult.h"
#include "Shared/Hand.h"
#include "Shared/PumpResults.h"
#include "Shared/RestoreSnapshotPlan.h"
#include "Shared/SlotPressAction.h"
#include "Shared/SpellSettings.h"
#include "Shared/SpellType.h"
#include "Shared/StateExitResult.h"

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

    enum class AutoCastPhase : std::uint8_t { Idle = 0, StartRequested, Casting, WaitingChargeRelease, Done };

    struct HandMode {
        IntegratedMagic::ActivationMode mode{IntegratedMagic::ActivationMode::Hold};
        bool wantAutoAttack{true};
        bool pressActive{false};
        bool holdActive{false};
        bool autoActive{false};
        bool waitingChargeComplete{false};
        bool chargeComplete{false};
        bool holdFiredAndWaitingCastStop{false};
        bool finished{false};
        bool pendingRestartNextFrame{false};
        float startRequestSecs{0.f};
        float castingElapsedSecs{0.f};
        bool waitingSpellFireFinalize{false};
        float spellFireFinalizeSecs{0.f};
        AutoCastPhase autoCastPhase{AutoCastPhase::Idle};
    };

    struct SessionState {
        bool active{false};
        int activeSlot{-1};
        bool isDualCasting{false};
        bool wasHandsDown{false};
        float activeTimeoutSecs{0.f};

        RE::SpellItem* modeSpellLeft{nullptr};
        RE::SpellItem* modeSpellRight{nullptr};

        RE::FormID activeLeftID{0};
        RE::FormID activeRightID{0};
        RE::FormID activeShoutID{0};

        void Reset() { *this = {}; }
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

    class MagicState {
    public:
        static MagicState& Get();

        [[nodiscard]] SlotPressAction OnSlotPressed(int slot);
        StateExitResult OnSlotReleased(int slot);
        [[nodiscard]] AttackEnabledResult OnEquipComplete();
        void NotifyUnexpectedUnequip(RE::TESBoundObject* base);

        StateExitResult OnCastStop();
        StateExitResult OnShoutStop();
        StateExitResult ForceExit();
        [[nodiscard]] StateExitResult ForceExitNoRestore();

        PumpAutomaticResult PumpAutomatic(float dt);
        PumpResult PumpAutoAttack(float dt);
        StateExitResult TryFinalizeExit();

        [[nodiscard]] AttackEnabledResult NotifyAttackEnabled();

        bool IsActive() const noexcept { return _session.active; }
        int ActiveSlot() const noexcept { return _session.activeSlot; }
        RE::FormID ActiveLeftID() const noexcept { return _session.activeLeftID; }
        RE::FormID ActiveRightID() const noexcept { return _session.activeRightID; }
        RE::FormID ActiveShoutID() const noexcept { return _session.activeShoutID; }
        bool IsDualCasting() const noexcept { return _session.isDualCasting; }
        bool IsWaitingSheatheRestore() const noexcept {
            return _restore.pendingRestoreAfterSheathe && !_restore.sheatheAnimComplete;
        }
        bool IsPressMode() const noexcept { return _left.pressActive || _right.pressActive; }
        void NotifySheatheComplete() noexcept { _restore.sheatheAnimComplete = true; }
        SpellFiredResult OnSpellFired(Hand hand);
        const HandMode& LeftMode() const noexcept { return _left; }
        const HandMode& RightMode() const noexcept { return _right; }
        bool IsInSlotSetup() const noexcept { return _inSlotSetup; }
        [[nodiscard]] bool IsShoutActive() const noexcept { return _shout.modeShoutID != 0; }
        void ScheduleSpellFireFinalize(Hand hand);
        void FinalizeRestoreSnapshotPlan(bool resetShout = false);
        void ResetShoutState() { _shout.Reset(); }
        void OnCasterStartCast(Hand hand, const RE::MagicItem* spell, RE::MagicSystem::CastingType type);
        [[nodiscard]] CastInterruptResult OnCasterInterrupt(Hand hand, const RE::MagicItem* spell, bool depleteEnergy);

    private:
        MagicState() = default;

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
            bool needsSkipEquipVars{false};
        };

        void ResetHandStates() {
            _left = {};
            _right = {};
            _aa.Reset();
            _session.isDualCasting = false;
            _session.activeTimeoutSecs = 0.f;
            _session.modeSpellLeft = nullptr;
            _session.modeSpellRight = nullptr;
        }

        void ResetSessionState() {
            ResetHandStates();
            ResetShoutState();
            _restore.ClearDirty();
            _session.active = false;
            _session.activeSlot = -1;
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

        [[nodiscard]] bool EnsureActiveWithSnapshot(RE::PlayerCharacter const* player, int slot,
                                                    bool raiseHandsIfSheathed = true);
        void CaptureSnapshot(RE::PlayerCharacter const* player);
        [[nodiscard]] RestoreSnapshotPlan BuildRestoreSnapshotPlan(RE::PlayerCharacter* player);
        void FinalizeExitAfterController();

        bool HandIsRelevant(Hand h) const;
        bool AllRelevantHandsFinished() const;
        bool CanOverwriteNow() const;
        bool ShouldForceInterrupt() const;

        StateExitResult ExitAllNow();
        PrepareOverwriteResult PrepareForOverwriteToSlot(int newSlot);
        DisableHandResult DisableHand(Hand hand);

        bool PrepareSlotEntry(int slot, SlotEntry& out);
        void EnterHand(Hand hand, const SpellSettings& ss, bool skipAnim);
        void TogglePressHand(Hand hand, const SpellSettings& ss);
        float FinishHand(Hand hand);
        void SetModeSpellsFromHand(Hand hand, RE::SpellItem* spell);

        [[nodiscard]] PumpCastPhaseResult PumpCastPhase(Hand hand, float dt);
        StateExitResult PumpSpellFireFinalize(float dt);
        bool RequestAutoAttackStart(Hand hand, bool clearWaitAfterEquip);
        void ConfirmAutoCastStarted(Hand hand);

        HandMode _left{};
        HandMode _right{};

        SessionState _session{};
        RestoreContext _restore{};
        AutoAttackState _aa{};
        ShoutState _shout{};
        bool _inSlotSetup{false};

        static constexpr float kMaxActiveTimeoutSecs = 30.f;
    };

}
