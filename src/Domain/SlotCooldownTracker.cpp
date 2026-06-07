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

        struct VoiceSlotResult {
            int slot{-1};
            RE::FormID formID{0};
            bool isPower{false};
        };

        VoiceSlotResult FindEquippedVoiceSlot() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return {};

            RE::FormID equippedID = 0;
            bool isPower = false;

            const auto& rd = player->GetActorRuntimeData();
            if (auto* selected = rd.selectedPower; selected) {
                if (auto* shout = selected->As<RE::TESShout>()) {
                    equippedID = shout->GetFormID();
                } else if (auto* power = selected->As<RE::SpellItem>()) {
                    using ST = RE::MagicSystem::SpellType;
                    const auto t = power->GetSpellType();
                    if (t == ST::kPower || t == ST::kLesserPower) {
                        equippedID = power->GetFormID();
                        isPower = true;
                    }
                }
            }

            if (!equippedID) return {};

            const int slotCount = static_cast<int>(Slots::GetSlotCount());
            for (int i = 0; i < slotCount; ++i) {
                if (Slots::GetSlotShout(i) == equippedID)
                    return {i, equippedID, isPower};
            }

            return {};
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

    void SlotCooldownTracker::Update(float dt) {
        const float currentRemaining = GetRemainingShoutCooldown();
        const bool wasCoolingDown = _prevRemainingCooldown > kCooldownEpsilon;
        const bool isCoolingDown = currentRemaining > kCooldownEpsilon;
        const bool justStarted = !wasCoolingDown && isCoolingDown;
        const bool justFinishedGlobal = wasCoolingDown && !isCoolingDown;

        for (auto& s : _slots) {
            s.justFinished = false;
        }

        if (justStarted) {
            const auto found = FindEquippedVoiceSlot();
            MAGIC_DEBUG_LOG("[Cooldown] justStarted: voiceTimer={:.1f}s found.slot={} found.formID={:#010x} found.isPower={}",
                            currentRemaining, found.slot, found.formID, found.isPower);
            if (found.slot >= 0 && found.slot < kMaxTrackedSlots) {
                auto& st = _slots[found.slot];
                st.trackedFormID = found.formID;
                st.isPower = found.isPower;
                st.onCooldown = true;
                st.justFinished = false;

                if (found.isPower) {
                    st.variationIndex = -1;
                    st.totalCooldown = currentRemaining;
                    st.remainingCooldown = currentRemaining;
                    MAGIC_DEBUG_LOG("[Cooldown] power start: slot={} formID={:#010x} total={:.1f}s",
                                    found.slot, found.formID, currentRemaining);
                } else {
                    const auto used = InferUsedVariation(found.formID, currentRemaining);
                    st.variationIndex = used.index;
                    st.totalCooldown = used.totalCooldown;
                    st.remainingCooldown = currentRemaining;
                    MAGIC_DEBUG_LOG("[Cooldown] shout start: slot={} shoutID={:#010x} remaining={:.3f} "
                                    "variation={} total={:.3f} raw={:.3f}",
                                    found.slot, found.formID, currentRemaining,
                                    used.index, used.totalCooldown, used.rawRecovery);
                }
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

            if (st.isPower) {
                if (st.onCooldown) {
                    const float prevRemaining = st.remainingCooldown;
                    st.remainingCooldown -= dt;
                    const int prevBucket = static_cast<int>(prevRemaining / 30.0f);
                    const int currBucket = static_cast<int>(st.remainingCooldown / 30.0f);
                    if (currBucket != prevBucket) {
                        MAGIC_DEBUG_LOG("[Cooldown] power tick: slot={} formID={:#010x} remaining={:.0f}s/{:.0f}s progress={:.1f}%",
                                        i, st.trackedFormID, st.remainingCooldown, st.totalCooldown,
                                        ComputeProgress(st.remainingCooldown, st.totalCooldown) * 100.0f);
                    }
                    if (st.remainingCooldown <= kCooldownEpsilon) {
                        st.justFinished = true;
                        st.remainingCooldown = 0.0f;
                        st.onCooldown = false;
                        MAGIC_DEBUG_LOG("[Cooldown] power finish: slot={} formID={:#010x} total={:.1f}s",
                                        i, st.trackedFormID, st.totalCooldown);
                    }
                }
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

    void SlotCooldownTracker::StartPowerCooldown(int slot, RE::FormID formID, float totalCooldown) {
        if (slot < 0 || slot >= kMaxTrackedSlots || !formID || totalCooldown <= kCooldownEpsilon) return;
        auto& st = _slots[slot];
        if (st.onCooldown && st.trackedFormID == formID) return;
        st.trackedFormID = formID;
        st.isPower = true;
        st.onCooldown = true;
        st.justFinished = false;
        st.variationIndex = -1;
        st.totalCooldown = totalCooldown;
        st.remainingCooldown = totalCooldown;
        MAGIC_DEBUG_LOG("[Cooldown] power start (restore-path): slot={} formID={:#010x} total={:.1f}s",
                        slot, formID, totalCooldown);
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
        out.isPower = st.isPower;
        out.progress = ComputeProgress(st.remainingCooldown, st.totalCooldown);

        return out;
    }

}