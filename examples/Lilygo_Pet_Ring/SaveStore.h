#pragma once

#include <Preferences.h>
#include "SaveCodec.h"

namespace pet {

class SaveStore {
 public:
  enum class Load { Fresh, Loaded, Corrupt, Unavailable };
  Load begin(State& state) {
    if (!prefs_.begin("lily-pet", false)) return Load::Unavailable;
    open_ = true;
    const uint8_t language = prefs_.getUChar("language", 1);
    language_ = language == 1 ? Language::English : Language::Chinese;
    SaveRecord a{}, b{};
    const size_t aSize = prefs_.getBytesLength("slot0");
    const size_t bSize = prefs_.getBytesLength("slot1");
    const bool aValid = read("slot0", a);
    const bool bValid = read("slot1", b);
    if (!aValid && !bValid) {
      // 损坏或未知版本的旧存档保留原样，不自动用新宠物覆盖。
      writable_ = aSize == 0 && bSize == 0;
      return writable_ ? Load::Fresh : Load::Corrupt;
    }
    slot_ = bValid && (!aValid || newerSequence(b.sequence, a.sequence)) ? 1 : 0;
    const SaveRecord& selected = slot_ ? b : a;
    state = selected.state;
    sequence_ = selected.sequence;
    writable_ = true;
    return Load::Loaded;
  }

  Language language() const { return language_; }
  bool setLanguage(Language value) {
    if (value != Language::Chinese && value != Language::English) return false;
    if (!open_ || prefs_.putUChar("language", static_cast<uint8_t>(value)) != 1) return false;
    language_ = value;
    return true;
  }

  bool save(const State& state) {
    if (!open_ || !writable_ || !valid(state)) return false;
    const int target = slot_ == 0 ? 1 : 0;
    const char* key = target ? "slot1" : "slot0";
    const SaveRecord record = makeRecord(state, sequence_ + 1U);
    if (prefs_.putBytes(key, &record, sizeof(record)) != sizeof(record))
      return false;
    SaveRecord verified{};
    if (!read(key, verified) || verified.sequence != record.sequence ||
        verified.crc != record.crc)
      return false;
    slot_ = target;
    sequence_ = record.sequence;
    return true;
  }

 private:
  bool read(const char* key, SaveRecord& record) {
    const size_t length = prefs_.getBytesLength(key);
    if (length != sizeof(SaveRecord) && length != sizeof(LegacySaveRecord)) return false;
    uint8_t bytes[sizeof(SaveRecord)]{};
    return prefs_.getBytes(key, bytes, length) == length &&
           decodeRecord(bytes, length, record);
  }
  Preferences prefs_;
  Language language_ = Language::English;
  uint32_t sequence_ = 0;
  int slot_ = -1;
  bool open_ = false;
  bool writable_ = false;
};

}  // namespace pet
