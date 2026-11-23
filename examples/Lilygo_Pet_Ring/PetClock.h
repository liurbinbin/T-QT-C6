#pragma once

#include "PetModel.h"

namespace pet {

inline void elapse(State& s, uint32_t seconds, bool clockKnown) {
  advance(s, seconds);
  if (clockKnown) {
    s.lastEpoch += seconds;
  } else {
    const uint32_t limit = 10U * 365U * kDay;
    s.untrustedSeconds = seconds >= limit - s.untrustedSeconds
                             ? limit : s.untrustedSeconds + seconds;
  }
}

inline bool synchronizeClock(State& s, uint32_t epoch,
                             uint32_t utcOffsetSeconds = 8U * 3600U) {
  if (epoch < 1704067200U || epoch >= 4102444800U ||
      (s.lastEpoch && epoch < s.lastEpoch)) return false;
  if (s.lastEpoch) {
    const uint32_t elapsed = epoch - s.lastEpoch;
    // 未知时间下已养成的在线秒数必须扣除，恢复时不能重复计算。
    if (elapsed > s.untrustedSeconds) advance(s, elapsed - s.untrustedSeconds, false);
  }
  s.lastEpoch = epoch;
  s.untrustedSeconds = 0;
  s.clockTrusted = 1;
  const uint32_t local = (epoch % kDay + utcOffsetSeconds % kDay) % kDay;
  s.clockOffsetSeconds = (local + kDay - s.ageSeconds % kDay) % kDay;
  return true;
}

}  // namespace pet
