#pragma once

#include "PCH.h"
#include "Shared/Hand.h"
#include "Shared/InventoryType.h"
#include "Shared/SlotPressResult.h"

namespace IntegratedMagic {

    struct EquipIntent {
        RE::SpellItem* spell{nullptr};
        Hand hand{Hand::Right};
    };

    struct SpellFiredResult {
        struct StopEvent {
            Hand hand;
            float heldSecs{-1.f};
        };

        std::optional<StopEvent> leftAttack;
        std::optional<StopEvent> rightAttack;
        bool finalizeLeft{false};
        bool finalizeRight{false};
    };

    struct DelayedStartsResult {
        bool dispatchLeft{false};
        bool dispatchRight{false};
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

    struct DisableHandResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        std::optional<StopEvent> attack;
    };

    struct PumpAutomaticHandResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        std::optional<StopEvent> attack;
    };

    struct CastInterruptResult {
        float finishedLeft = -1.f;
        float finishedRight = -1.f;
    };

    struct PrepareOverwriteResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        std::optional<StopEvent> leftAttack;
        std::optional<StopEvent> rightAttack;
    };

    struct PumpAutoStartFallbackResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        bool startAttack{false};
        std::optional<StopEvent> stopAttack;
    };

    struct RestoreSnapshotPlan {
        bool valid{false};

        bool applySkipEquipAnimReturn{false};

        InventoryIndex inventoryIndex{};

        bool restoreRightHand{false};
        bool restoreLeftHand{false};

        ObjSnapshot rightObj{};
        ObjSnapshot leftObj{};

        bool clearRightHand{false};
        RE::SpellItem* clearRightHandByRef{nullptr};

        bool clearLeftHand{false};
        RE::SpellItem* clearLeftHandByRef{nullptr};

        RE::SpellItem* equipRightSpell{nullptr};
        RE::SpellItem* equipLeftSpell{nullptr};

        bool restoreRightAfterLeftOnly{false};

        bool clearVoiceShout{false};
        RE::TESForm* equipVoiceForm{nullptr};
        std::vector<ExtraEquippedItem> prevExtraEquipped{};
    };

    struct PumpAutomaticResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        bool startLeftAttack{false};
        bool startRightAttack{false};

        std::optional<StopEvent> stopLeftAttack;
        std::optional<StopEvent> stopRightAttack;
        std::optional<StopEvent> stopShout;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool finalizeAfterExecution{false};
        bool resetShoutAfterExecution{false};
    };

    struct ForceExitResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        std::optional<StopEvent> leftAttack;
        std::optional<StopEvent> rightAttack;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool finalizeAfterController{false};
        bool resetShoutAfterController{false};
    };

    struct ExitAllResult {
        struct StopEvent {
            float heldSecs{0.f};
        };

        std::optional<StopEvent> leftAttack;
        std::optional<StopEvent> rightAttack;
        std::optional<StopEvent> shout;

        std::optional<RestoreSnapshotPlan> restorePlan;

        bool waitForSheatheRestore{false};
        bool waitForPendingRestore{false};
        bool waitForPowerRestore{false};

        bool finalizeExitAfterController{false};
    };

    struct SlotPressAction {
        SlotPressResult result{SlotPressResult::None};
        std::vector<EquipIntent> spellsToEquip;
        RE::TESForm* shoutToEquip{nullptr};
        bool startShoutDispatch{false};
        bool skipAnim{false};
        InventoryIndex inventorySnapshotBefore{};

        std::optional<ExitAllResult::StopEvent> leftAttack;
        std::optional<ExitAllResult::StopEvent> rightAttack;
        std::optional<ExitAllResult::StopEvent> shout;

        std::optional<RestoreSnapshotPlan> restorePlan;
        bool finalizeAfterController{false};
        bool resetShoutAfterController{false};
    };

    struct ProcessButtonEventsResult {
        std::optional<ForceExitResult> forceExit;
    };

    struct RetainedEvent {
        RE::INPUT_DEVICE dev;
        std::uint32_t rawIdCode;
        RE::BSFixedString userEvent;
        float value;
        float heldSecs;
    };

    struct DrainDeferredReplayResult {
        std::optional<RetainedEvent> replayEvent;
    };
}