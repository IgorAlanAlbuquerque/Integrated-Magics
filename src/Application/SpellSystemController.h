#pragma once
#include "PCH.h"
#include "Shared/Hand.h"
#include "Domain/State.h"

namespace Application {

    class SpellSystemController {
    public:
        static SpellSystemController& Get();

        void OnFrame(float dt, bool inputBlocked) const;
        void NotifyAnimEvent(std::string_view tag) const;
        void NotifyPlayerDeath() const;
        void NotifyLoadGame() const;
        void NotifyMenuOpen(std::string_view menuName) const;
        void TryAssignHoveredToSlotByHotkey() const;
        void OnConfigChanged() const;
        void NotifyForeignEquip() const;
        [[nodiscard]] bool IsSpellSystemActive() const;
        [[nodiscard]] int ActiveSlot() const;
        [[nodiscard]] bool IsInSlotSetup() const;
        [[nodiscard]] bool IsShoutActive() const;
        void ConsumeForceExitResult(IntegratedMagic::ForceExitResult result) const;

    private:
        SpellSystemController() = default;
        void DispatchSlotEvents() const;
        void HandleExitAllResult(IntegratedMagic::ExitAllResult result) const;
        void ExecuteRestoreSnapshotPlan(const IntegratedMagic::RestoreSnapshotPlan& plan) const;
        void HandleForceExitResult(IntegratedMagic::ForceExitResult result) const;
    };

}