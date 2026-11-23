#pragma once

#include <stdint.h>

namespace pet {

struct Motion {
  // 单位与仓库 demo 一致：加速度 mg，角速度 mdps。
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t az = 0;
  int32_t gx = 0;
  int32_t gy = 0;
  int32_t gz = 0;
  uint32_t timestampMs = 0;
};

inline int32_t imuSignedWord(const uint8_t* bytes) {
  const uint32_t raw = static_cast<uint32_t>(bytes[0]) |
                       (static_cast<uint32_t>(bytes[1]) << 8U);
  return raw >= 32768U ? static_cast<int32_t>(raw) - 65536
                       : static_cast<int32_t>(raw);
}

inline Motion decodeImuSample(const uint8_t* bytes, uint32_t timestampMs) {
  Motion sample;
  // OUTX_L_G 起连续 12 字节；量程必须为 ±2000 dps、±16 g。
  sample.gx = imuSignedWord(bytes) * 70;
  sample.gy = imuSignedWord(bytes + 2) * 70;
  sample.gz = imuSignedWord(bytes + 4) * 70;
  sample.ax = static_cast<int16_t>(imuSignedWord(bytes + 6) * 488 / 1000);
  sample.ay = static_cast<int16_t>(imuSignedWord(bytes + 8) * 488 / 1000);
  sample.az = static_cast<int16_t>(imuSignedWord(bytes + 10) * 488 / 1000);
  sample.timestampMs = timestampMs;
  return sample;
}

}  // namespace pet
