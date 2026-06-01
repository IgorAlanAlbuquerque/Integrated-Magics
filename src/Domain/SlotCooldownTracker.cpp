#include "Domain/SlotCooldownTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Config/Slots.h"
#include "PCH.h"

namespace IntegratedMagic {

    namespace {
        constexpr float kCooldownEpsilon = 0.001f;

        float GetRemainingShoutCooldown() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return 0.0f;
            return player->GetVoiceRecoveryTime();
        }

        float GetShoutRecoveryMult() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return 1.0f;

            if (auto* avo = player->AsActorValueOwner(); avo) {
                return avo->GetActorValue(RE::ActorValue::kShoutRecoveryMult);
            }
            return 1.0f;
        }

        RE::TESShout* LookupShout(RE::FormID shoutID) {
            if (!shoutID) return nullptr;
            return RE::TESForm::LookupByID<RE::TESShout>(shoutID);
        }

        bool IsSameShoutForm(RE::FormID slotFormID, RE::FormID equippedFormID) {
            return slotFormID != 0 && equippedFormID != 0 && slotFormID == equippedFormID;
        }

        int FindSlotForCurrentlyEquippedShout() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return -1;

            RE::FormID equippedShoutID = 0;

            const auto& rd = player->GetActorRuntimeData();
            if (auto* selected = rd.selectedPower; selected) {
                if (auto* shout = selected->As<RE::TESShout>()) {
                    equippedShoutID = shout->GetFormID();
                }
            }

            if (!equippedShoutID) return -1;

            const int slotCount = static_cast<int>(Slots::GetSlotCount());
            for (int i = 0; i < slotCount; ++i) {
                if (Slots::GetSlotShout(i) == equippedShoutID) return i;
            }

            return -1;
        }

        struct UsedVariationResult {
            int index = -1;
            float totalCooldown = 0.0f;
            float rawRecovery = 0.0f;
        };

        UsedVariationResult InferUsedVariation(RE::FormID shoutID, float remainingCooldown) {
            UsedVariationResult out{};

            auto* shout = RE::TESForm::LookupByID<RE::TESShout>(shoutID);
            if (!shout || remainingCooldown <= 0.0f) return out;

            const float mult = GetShoutRecoveryMult();
            float bestDelta = std::numeric_limits<float>::max();

            int i = 0;
            for (const auto& variation : shout->variations) {
                const float expected = variation.recoveryTime * mult;
                const float delta = std::abs(expected - remainingCooldown);

                if (delta < bestDelta) {
                    bestDelta = delta;
                    out.index = i;
                    out.totalCooldown = expected;
                    out.rawRecovery = variation.recoveryTime;
                }
                i++;
            }

            return out;
        }

        float ComputeProgress(float remaining, float total) {
            if (total <= kCooldownEpsilon) return 1.0f;
            return std::clamp(1.0f - (remaining / total), 0.0f, 1.0f);
        }
    }

    SlotCooldownTracker& SlotCooldownTracker::Get() {
        static SlotCooldownTracker instance;
        return instance;
    }

    void SlotCooldownTracker::Reset() {
        _prevRemainingCooldown = 0.0f;
        for (auto& s : _slots) {
            s = {};
        }
    }

    void SlotCooldownTracker::Update(float) {
        const float currentRemaining = GetRemainingShoutCooldown();
        const bool wasCoolingDown = _prevRemainingCooldown > kCooldownEpsilon;
        const bool isCoolingDown = currentRemaining > kCooldownEpsilon;
        const bool justStarted = !wasCoolingDown && isCoolingDown;
        const bool justFinishedGlobal = wasCoolingDown && !isCoolingDown;

        for (auto& s : _slots) {
            s.justFinished = false;
        }

        if (justStarted) {
            const int slot = FindSlotForCurrentlyEquippedShout();
            if (slot >= 0 && slot < kMaxTrackedSlots) {
                const RE::FormID shoutID = Slots::GetSlotShout(slot);
                const auto used = InferUsedVariation(shoutID, currentRemaining);

                auto& st = _slots[slot];
                st.trackedFormID = shoutID;
                st.variationIndex = used.index;
                st.totalCooldown = used.totalCooldown;
                st.remainingCooldown = currentRemaining;
                st.onCooldown = true;
                st.justFinished = false;

                MAGIC_DEBUG_LOG(
                    "[Cooldown] start: slot={} shoutID={:#010x} remaining={:.3f} variation={} total={:.3f} raw={:.3f}",
                    slot, shoutID, currentRemaining, used.index, used.totalCooldown, used.rawRecovery);
            }
        }

        for (int i = 0; i < kMaxTrackedSlots; ++i) {
            auto& st = _slots[i];

            if (!st.trackedFormID) continue;

            const RE::FormID currentSlotShout = Slots::GetSlotShout(i);
            if (currentSlotShout != st.trackedFormID) {
                MAGIC_DEBUG_LOG("[Cooldown] slot={} shout changed old={:#010x} new={:#010x} -> clear", i,
                                st.trackedFormID, currentSlotShout);
                st = {};
                continue;
            }

            if (isCoolingDown) {
                st.remainingCooldown = currentRemaining;
                st.onCooldown = true;
            } else {
                if (st.onCooldown) {
                    st.justFinished = true;
                    MAGIC_DEBUG_LOG("[Cooldown] finish: slot={} shoutID={:#010x} variation={} total={:.3f}", i,
                                    st.trackedFormID, st.variationIndex, st.totalCooldown);
                }

                st.remainingCooldown = 0.0f;
                st.onCooldown = false;
            }
        }

        if (justFinishedGlobal) {
        }

        _prevRemainingCooldown = currentRemaining;
    }

    SlotCooldownInfo SlotCooldownTracker::GetSlotInfo(int slot) const {
        SlotCooldownInfo out{};

        if (slot < 0 || slot >= kMaxTrackedSlots) return out;

        const auto& st = _slots[slot];
        out.formID = st.trackedFormID;
        out.variationIndex = st.variationIndex;
        out.totalCooldown = st.totalCooldown;
        out.remainingCooldown = st.remainingCooldown;
        out.onCooldown = st.onCooldown;
        out.justFinished = st.justFinished;
        out.progress = ComputeProgress(st.remainingCooldown, st.totalCooldown);

        return out;
    }

}