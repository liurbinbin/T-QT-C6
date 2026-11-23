#pragma once

#include "PetModel.h"
#include <stddef.h>
#include <string.h>

namespace pet {

constexpr size_t kLegacyStateBytes = offsetof(State, growthStage);
static_assert(kLegacyStateBytes == 39U * sizeof(uint32_t), "V1 save prefix must remain unchanged");

struct LegacySaveRecord {
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t stateBytes;
  uint32_t state[kLegacyStateBytes / sizeof(uint32_t)];
  uint32_t crc;
};

struct SaveRecord {
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t stateBytes;
  State state;
  uint32_t crc;
};

inline uint32_t checksum(const void* data, size_t bytes) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < bytes; ++i) {
    crc ^= p[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}

inline SaveRecord makeRecord(const State& state, uint32_t sequence) {
  SaveRecord r{};
  r.magic = 0x59415941U;
  r.version = 2;
  r.sequence = sequence;
  r.stateBytes = sizeof(State);
  r.state = state;
  r.crc = checksum(&r, offsetof(SaveRecord, crc));
  return r;
}

inline bool validRecord(const SaveRecord& r) {
  return r.magic == 0x59415941U && r.version == 2 &&
         r.stateBytes == sizeof(State) && valid(r.state) &&
         r.crc == checksum(&r, offsetof(SaveRecord, crc));
}

inline bool decodeRecord(const void* bytes, size_t length, SaveRecord& output) {
  if (length == sizeof(SaveRecord)) {
    SaveRecord current{};
    memcpy(&current, bytes, sizeof(current));
    if (!validRecord(current)) return false;
    output = current;
    return true;
  }
  if (length != sizeof(LegacySaveRecord)) return false;
  LegacySaveRecord old{};
  memcpy(&old, bytes, sizeof(old));
  if (old.magic != 0x59415941U || old.version != 1 ||
      old.stateBytes != kLegacyStateBytes ||
      old.crc != checksum(&old, offsetof(LegacySaveRecord, crc))) return false;
  State migrated = newState(1);
  memcpy(&migrated, old.state, kLegacyStateBytes);
  initializeProgress(migrated, true);
  if (!valid(migrated)) return false;
  output = makeRecord(migrated, old.sequence);
  return true;
}

inline bool newerSequence(uint32_t a, uint32_t b) {
  return a != b && (a - b) < 0x80000000U;
}

}  // namespace pet
