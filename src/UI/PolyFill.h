#pragma once
#include <vector>

#include "UI/StyleConfig.h"

namespace IntegratedMagic::PolyFill {

    struct Triangle {
        float ax, ay;
        float bx, by;
        float cx, cy;
    };

    inline std::vector<Triangle> Triangulate(const std::vector<SlotShapeVertex>& verts, float centerX, float centerY,
                                             float r) {
        const auto n = static_cast<int>(verts.size());
        if (n < 3) return {};

        float cx = 0.f, cy = 0.f;
        for (const auto& v : verts) {
            cx += centerX + v.x * r;
            cy += centerY + v.y * r;
        }
        cx /= n;
        cy /= n;

        std::vector<Triangle> tris;
        tris.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            tris.push_back({cx, cy, centerX + verts[i].x * r, centerY + verts[i].y * r, centerX + verts[j].x * r,
                            centerY + verts[j].y * r});
        }
        return tris;
    }
}