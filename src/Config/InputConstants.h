#pragma once
#include <cstdint>

#include "Config/Limits.h"

inline constexpr int kInputMaxSlots = static_cast<int>(IntegratedMagic::Config::kMaxSlots);
inline constexpr int kMaxCode = 400;
inline constexpr int kMouseButtonBase = 256;
inline constexpr float kFilterReplayDelaySec = 0.04f;
inline constexpr float kPressBothAtSameTimeWindowSec = 0.2f;

inline constexpr int kDIK_W = 0x11;
inline constexpr int kDIK_A = 0x1E;
inline constexpr int kDIK_S = 0x1F;
inline constexpr int kDIK_D = 0x20;
inline constexpr int kDIK_Escape = 0x01;