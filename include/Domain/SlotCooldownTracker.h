#pragma once

#include "PCH.h"

namespace IntegratedMagic {

    struct SlotCooldownInfo {
        bool onCooldown{false};
        float progress{1.0f};
        bool justFinished{false};
        RE::FormID formID{0};
        int variationIndex{-1};
        float totalCooldown{0.0f};
        float remainingCooldown{0.0f};
        bool isPower{false};
    };

    class SlotCooldownTracker {
    public:
        static SlotCooldownTracker& Get();

        void Update(float dt);
        void Reset();
        void StartPowerCooldown(int slot, RE::FormID formID, float totalCooldown);

        [[nodiscard]] SlotCooldownInfo GetSlotInfo(int slot) const;

    private:
        SlotCooldownTracker() = default;

        static constexpr int kMaxTrackedSlots = 64;

        struct SlotState {
            RE::FormID trackedFormID{0};
            int variationIndex{-1};
            float totalCooldown{0.0f};
            float remainingCooldown{0.0f};
            bool onCooldown{false};
            bool justFinished{false};
            bool isPower{false};
        };

        float _prevRemainingCooldown{0.0f};
        SlotState _slots[kMaxTrackedSlots]{};
    };

}