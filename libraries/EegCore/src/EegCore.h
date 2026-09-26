#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace eeg {
static const char kVersion[] = "0.2.0";
static const char kServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char kNotifyUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static const size_t kMaxPacket = 173;

inline uint32_t read32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}
inline void write32(uint8_t* p, uint32_t n) {
  p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
inline bool checksum(const uint8_t* p, size_t n) {
  if (n < 4 || p[0] != 0xAA || p[1] != 0xAA || p[2] > 169 || n != size_t(p[2]) + 4) return false;
  uint8_t sum = 0;
  for (size_t i = 3; i < n; ++i) sum += p[i];
  return sum == 0xFF;
}
inline bool raw(const uint8_t* p, size_t n, int16_t& value) {
  if (n != 8 || p[2] != 4 || p[3] != 0x80 || p[4] != 2 || !checksum(p, n)) return false;
  value = int16_t((uint16_t(p[5]) << 8) | p[6]);
  return true;
}
inline void rawPacket(uint8_t* p, uint8_t hi, uint8_t lo) {
  const uint8_t bytes[] = {0xAA, 0xAA, 4, 0x80, 2, hi, lo,
                         uint8_t(0xFF - uint8_t(0x82 + hi + lo))};
  memcpy(p, bytes, 8);
}

struct Metrics {
  int signal = -1, attention = -1, meditation = -1, blink = -1;
  bool hasBands = false;
  uint32_t bands[8] = {};
};

// Decode into a temporary; malformed tails must never partially update live state.
inline bool metrics(const uint8_t* p, size_t n, Metrics& out) {
  if (!checksum(p, n)) return false;
  Metrics next;
  size_t i = 3, end = n - 1;
  while (i < end) {
    unsigned extended = 0;
    while (i < end && p[i] == 0x55) { ++extended; ++i; }
    if (i == end) return false;
    const uint8_t code = p[i++];
    size_t length = 1;
    if (code >= 0x80) {
      if (i == end) return false;
      length = p[i++];
    }
    if (length > end - i) return false;
    if (!extended) {
      if (code == 2) { if (p[i] > 200) return false; next.signal = p[i]; }
      else if (code == 4) { if (p[i] > 100) return false; next.attention = p[i]; }
      else if (code == 5) { if (p[i] > 100) return false; next.meditation = p[i]; }
      else if (code == 0x16) next.blink = p[i];
      else if (code == 0x83) {
        if (length != 24) return false;
        for (size_t b = 0; b < 8; ++b) {
          const uint8_t* q = p + i + b * 3;
          next.bands[b] = (uint32_t(q[0]) << 16) | (uint32_t(q[1]) << 8) | q[2];
        }
        next.hasBands = true;
      }
    }
    i += length;
  }
  out = next;
  return true;
}

class Parser {
 public:
  uint32_t checksumErrors = 0, skippedBytes = 0, timeouts = 0;
  size_t pending() const { return used_; }
  void reset() { used_ = 0; }
  template <typename Handler>
  void feed(uint8_t byte, uint32_t now, Handler handler) {
    if (used_ && uint32_t(now - lastByte_) > 100) { ++timeouts; skippedBytes += used_; used_ = 0; }
    lastByte_ = now;
    if (used_ == sizeof(buffer_)) discard(1);
    buffer_[used_++] = byte;
    while (used_ >= 3) {
      if (buffer_[0] != 0xAA || buffer_[1] != 0xAA || buffer_[2] > 169 || buffer_[2] == 0) {
        discard(1); continue;
      }
      const size_t total = size_t(buffer_[2]) + 4;
      if (used_ < total) return;
      if (!checksum(buffer_, total)) { ++checksumErrors; discard(1); continue; }
      handler(buffer_, total);
      used_ -= total;
      memmove(buffer_, buffer_ + total, used_);
    }
  }
 private:
  uint8_t buffer_[kMaxPacket] = {};
  size_t used_ = 0;
  uint32_t lastByte_ = 0;
  void discard(size_t n) {
    skippedBytes += n; used_ -= n;
    memmove(buffer_, buffer_ + n, used_);
  }
};

// One stream sequence across all types. Gap count is EVENTS, not lost samples.
class BleAssembler {
 public:
  uint32_t gapEvents = 0, malformed = 0, duplicateMessages = 0;
  bool discontinuity = true;
  void reset() { sequenced_ = false; first_ = false; discontinuity = true; }
  template <typename PacketHandler, typename HealthHandler>
  bool accept(const uint8_t* p, size_t n, uint32_t now, PacketHandler packet, HealthHandler health) {
    if (n < 2 || n > 20) { ++malformed; first_ = false; return false; }
    if (sequenced_) {
      if (p[1] == sequence_) { ++duplicateMessages; first_ = false; return false; }
      if (uint8_t(sequence_ + 1) != p[1]) { ++gapEvents; first_ = false; discontinuity = true; }
    }
    sequence_ = p[1]; sequenced_ = true;
    if (first_ && uint32_t(now - firstMs_) > 500) { first_ = false; ++malformed; }
    if (p[0] == 3) {
      if (n != 20 || !first_) { ++malformed; first_ = false; return false; }
      first_ = false;
      memcpy(metrics_ + 18, p + 2, 18);
      if (!checksum(metrics_, 36)) { ++malformed; return false; }
      packet(metrics_, 36); return true;
    }
    first_ = false; // The two metrics halves must be adjacent.
    if (p[0] == 1 && n >= 4 && n % 2 == 0) {
      for (size_t i = 2; i < n; i += 2) {
        uint8_t bytes[8]; rawPacket(bytes, p[i], p[i + 1]); packet(bytes, 8);
      }
      return true;
    }
    if (p[0] == 2 && n == 20) {
      memcpy(metrics_, p + 2, 18); first_ = true; firstMs_ = now; return true;
    }
    if (p[0] == 4 && n == 20 && p[2] == 1) { health(p + 2); return true; }
    ++malformed; return false;
  }
 private:
  bool sequenced_ = false, first_ = false;
  uint8_t sequence_ = 0, metrics_[36] = {};
  uint32_t firstMs_ = 0;
};
} // namespace eeg
