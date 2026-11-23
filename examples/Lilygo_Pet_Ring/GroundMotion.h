#pragma once

#include <stdint.h>

namespace pet {

inline int16_t groundStep(int32_t tiltMg, uint32_t elapsedMs,
                          int32_t& remainder, int32_t divisor) {
  constexpr int32_t kDeadZoneMg = 100;
  if (tiltMg >= -kDeadZoneMg && tiltMg <= kDeadZoneMg) {
    remainder = 0;
    return 0;
  }
  int32_t drive = tiltMg > 0 ? tiltMg - kDeadZoneMg : tiltMg + kDeadZoneMg;
  if (drive > 900) drive = 900;
  if (drive < -900) drive = -900;
  if ((drive > 0 && remainder < 0) || (drive < 0 && remainder > 0))
    remainder = 0;
  // 长时间离开房间后不补算移动，避免菜单返回或唤醒时突然冲出一大段。
  if (elapsedMs > 100U) elapsedMs = 100U;
  remainder += drive * static_cast<int32_t>(elapsedMs);
  const int32_t pixels = remainder / divisor;
  remainder -= pixels * divisor;
  return static_cast<int16_t>(pixels);
}

}  // namespace pet
