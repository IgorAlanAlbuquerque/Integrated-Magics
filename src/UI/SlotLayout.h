#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

#include "UI/StyleConfig.h"

namespace IntegratedMagic {

    struct LayoutVec2 {
        float x = 0.f;
        float y = 0.f;
    };

    class SlotLayout {
    public:
        static constexpr int kMaxSlots = 64;
        static constexpr float kPI = std::numbers::pi_v<float>;

        static void Compute(HudLayoutType type, int n, float slotRadius, float ringRadius, float spacing,
                            int gridColumns, LayoutVec2* out) {
            switch (type) {
                case HudLayoutType::Horizontal:
                    ComputeLinear(n, slotRadius, spacing, false, out);
                    break;
                case HudLayoutType::Vertical:
                    ComputeLinear(n, slotRadius, spacing, true, out);
                    break;
                case HudLayoutType::Grid:
                    ComputeGrid(n, slotRadius, spacing, gridColumns, out);
                    break;
                case HudLayoutType::Circular:
                default:
                    ComputeCircular(n, slotRadius, ringRadius, out);
                    break;
            }
        }

        static LayoutVec2 BoundingHalf(HudLayoutType type, int n, float slotRadius, float ringRadius, float spacing,
                                       int gridColumns) {
            switch (type) {
                case HudLayoutType::Horizontal:
                    return {LinearExtent(n, slotRadius, spacing) * 0.5f, slotRadius};
                case HudLayoutType::Vertical:
                    return {slotRadius, LinearExtent(n, slotRadius, spacing) * 0.5f};
                case HudLayoutType::Grid: {
                    const int cols = std::max(1, std::min(gridColumns, n));
                    const int rows = (n + cols - 1) / cols;
                    return {LinearExtent(cols, slotRadius, spacing) * 0.5f,
                            LinearExtent(rows, slotRadius, spacing) * 0.5f};
                }
                case HudLayoutType::Circular:
                default: {
                    const float r = CircularRadius(n, slotRadius, ringRadius);
                    return {r + slotRadius, r + slotRadius};
                }
            }
        }

        static bool HasCenter(HudLayoutType type) noexcept { return type == HudLayoutType::Circular; }

        static float CircularRadius(int n, float slotRadius, float baseR, float gap = 8.f) {
            if (n <= 1) return baseR;
            const float minR = (slotRadius + gap * 0.5f) / std::sin(kPI / static_cast<float>(n));
            return std::max(baseR, minR);
        }

    private:
        static float LinearExtent(int count, float slotRadius, float spacing) {
            return count * slotRadius * 2.f + std::max(0, count - 1) * spacing;
        }

        static void ComputeCircular(int n, float slotRadius, float ringRadius, LayoutVec2* out) {
            const float r = CircularRadius(n, slotRadius, ringRadius);
            for (int i = 0; i < n; ++i) {
                const float angle = kPI + (2.f * kPI / static_cast<float>(n)) * static_cast<float>(i);
                out[i] = {r * std::cos(angle), r * std::sin(angle)};
            }
        }

        static void ComputeLinear(int n, float slotRadius, float spacing, bool vertical, LayoutVec2* out) {
            const float step = slotRadius * 2.f + spacing;
            const float start = -step * static_cast<float>(n - 1) * 0.5f;
            for (int i = 0; i < n; ++i) {
                const float p = start + step * static_cast<float>(i);
                out[i] = vertical ? LayoutVec2{0.f, p} : LayoutVec2{p, 0.f};
            }
        }

        static void ComputeGrid(int n, float slotRadius, float spacing, int gridColumns, LayoutVec2* out) {
            const int cols = std::max(1, std::min(gridColumns, n));
            const int rows = (n + cols - 1) / cols;
            const float stepX = slotRadius * 2.f + spacing;
            const float stepY = slotRadius * 2.f + spacing;
            const float startX = -stepX * static_cast<float>(cols - 1) * 0.5f;
            const float startY = -stepY * static_cast<float>(rows - 1) * 0.5f;
            for (int i = 0; i < n; ++i) {
                const int col = i % cols;
                const int row = i / cols;
                out[i] = {startX + stepX * static_cast<float>(col), startY + stepY * static_cast<float>(row)};
            }
        }
    };
}