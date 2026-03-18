#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "UI/StyleConfig.h"

namespace IntegratedMagic {
    class SlotAnimator {
    public:
        static constexpr int kMaxSlots = 8;

        static void Update(int n, int activeSlot, bool modifierHeld) {
            const float dt = ComputeDeltaTime();
            const auto& st = StyleConfig::Get();
            const float expandSpeed = st.slotExpandTime > 0.f ? 1.f / st.slotExpandTime : 9999.f;
            const float retractSpeed = st.slotRetractTime > 0.f ? 1.f / st.slotRetractTime : 9999.f;

            for (int i = 0; i < kMaxSlots; ++i) {
                float target = 1.f;

                if (i < n) {
                    if (activeSlot == i)
                        target = st.slotActiveScale;
                    else if (modifierHeld)
                        target = st.slotModifierScale;
                }

                s_slots[i].SetTarget(target);
                s_slots[i].Advance(dt, expandSpeed, retractSpeed);
            }
        }

        static float GetScale(int i) {
            if (i < 0 || i >= kMaxSlots) return 1.f;
            return s_slots[i].Get();
        }

        static float MaxPossibleScale() {
            const auto& st = StyleConfig::Get();
            return std::max({1.f, st.slotActiveScale, st.slotModifierScale});
        }

        static void Reset() {
            for (auto& s : s_slots) {
                s.current = 1.f;
                s.target = 1.f;
            }
        }

    private:
        struct SlotAnim {
            float current = 1.f;
            float target = 1.f;

            void SetTarget(float t) noexcept { target = t; }

            void Advance(float dt, float expandSpeed, float retractSpeed) noexcept {
                const float diff = target - current;
                if (std::abs(diff) < 0.0005f) {
                    current = target;
                    return;
                }
                const float speed = (diff > 0.f) ? expandSpeed : retractSpeed;

                current += diff * std::min(dt * speed, 1.f);
            }

            float Get() const noexcept { return current; }
        };

        static inline SlotAnim s_slots[kMaxSlots]{};

        static float ComputeDeltaTime() {
            using clock = std::chrono::steady_clock;
            static clock::time_point last = clock::now();
            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (dt < 0.f || dt > 0.25f) dt = 0.f;
            return dt;
        }
    };
}