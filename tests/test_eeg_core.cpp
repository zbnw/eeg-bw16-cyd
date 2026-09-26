#include <EegCore.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> frame(std::vector<uint8_t> payload) {
  std::vector<uint8_t> p = {0xAA, 0xAA, uint8_t(payload.size())};
  uint8_t sum = 0;
  for (uint8_t b : payload) { sum += b; p.push_back(b); }
  p.push_back(uint8_t(0xFF - sum));
  return p;
}
static std::vector<uint8_t> metricsFrame() {
  std::vector<uint8_t> p = {2, 0, 0x83, 24};
  for (int i = 0; i < 24; ++i) p.push_back(i);
  p.insert(p.end(), {4, 50, 5, 60});
  return frame(p);
}
int main(int argc, char** argv) {
  uint8_t raw[8]; eeg::rawPacket(raw, 0x80, 0);
  int16_t value = 0;
  assert(eeg::raw(raw, 8, value) && value == -32768);
  eeg::rawPacket(raw, 0x7F, 0xFF);
  assert(eeg::raw(raw, 8, value) && value == 32767);
  auto m = metricsFrame(); eeg::Metrics decoded;
  assert(eeg::metrics(m.data(), m.size(), decoded));
  assert(decoded.attention == 50 && decoded.meditation == 60 && decoded.hasBands);
  // Field order is not fixed, and extended codes must not overwrite basic fields.
  auto reordered = frame({5, 30, 0x55, 2, 200, 2, 0, 4, 70});
  assert(eeg::metrics(reordered.data(), reordered.size(), decoded));
  assert(decoded.signal == 0 && decoded.attention == 70 && decoded.meditation == 30);
  const auto invalid = frame({4, 99, 0x83, 24, 1});
  assert(!eeg::metrics(invalid.data(), invalid.size(), decoded));
  assert(decoded.attention == 70); // No partial commit.
  eeg::Parser parser;
  unsigned count = 0;
  auto handler = [&](const uint8_t* p, size_t n) { assert(eeg::checksum(p, n)); ++count; };
  // A corrupt candidate contains a valid nested raw frame; sliding recovery must find it.
  std::vector<uint8_t> damaged = {0xAA, 0xAA, 32, 0};
  damaged.insert(damaged.end(), raw, raw + 8);
  damaged.resize(36, 0);
  for (uint8_t b : damaged) parser.feed(b, 10, handler);
  assert(count == 1 && parser.checksumErrors >= 1);
  // Millisecond wrap and expiry of an incomplete header.
  parser.reset(); parser.feed(0xAA, 0xFFFFFFF0, handler);
  parser.feed(0xAA, 0xFFFFFFF1, handler);
  for (uint8_t b : std::vector<uint8_t>(raw, raw + 8)) parser.feed(b, 200, handler);
  assert(parser.timeouts == 1 && count == 2);
  // Sustained arbitrary UART input cannot overflow the fixed buffer or emit bad checksums.
  uint32_t random = 12345;
  for (uint32_t i = 0; i < 100000; ++i) {
    random = random * 1664525U + 1013904223U;
    parser.feed(uint8_t(random >> 24), 300 + i, handler);
    assert(parser.pending() < eeg::kMaxPacket);
  }
  eeg::BleAssembler assembler;
  unsigned delivered = 0, healthCount = 0;
  auto packet = [&](const uint8_t* p, size_t n) { assert(eeg::checksum(p, n)); ++delivered; };
  auto health = [&](const uint8_t*) { ++healthCount; };
  uint8_t first[20] = {2, 254}, second[20] = {3, 255};
  memcpy(first + 2, m.data(), 18); memcpy(second + 2, m.data() + 18, 18);
  assert(assembler.accept(first, 20, 0, packet, health));
  assert(assembler.accept(second, 20, 10, packet, health));
  uint8_t samples[4] = {1, 0, 0, 1};
  assert(assembler.accept(samples, 4, 11, packet, health));
  assert(delivered == 2 && assembler.gapEvents == 0);
  assert(!assembler.accept(samples, 4, 12, packet, health));
  assert(delivered == 2 && assembler.duplicateMessages == 1);
  first[1] = 1; second[1] = 3;
  assembler.accept(first, 20, 20, packet, health);
  assert(!assembler.accept(second, 20, 21, packet, health));
  assert(assembler.gapEvents == 1 && delivered == 2);
  first[1] = 4; second[1] = 6; samples[1] = 5;
  assembler.accept(first, 20, 30, packet, health);
  assembler.accept(samples, 4, 31, packet, health);
  assert(!assembler.accept(second, 20, 32, packet, health));
  first[1] = 7; second[1] = 8;
  assembler.accept(first, 20, 40, packet, health);
  assert(!assembler.accept(second, 20, 541, packet, health));
  uint8_t status[20] = {4, 9, 1, 0, 2, 0};
  assert(assembler.accept(status, 20, 542, packet, health) && healthCount == 1);
  assembler.reset(); samples[1] = 0;
  assert(assembler.accept(samples, 4, 550, packet, health));
  if (argc > 1) {
    std::ifstream in(argv[1], std::ios::binary);
    assert(in.good());
    eeg::Parser historical; unsigned rawCount = 0, metricsCount = 0;
    char b;
    while (in.get(b)) historical.feed(uint8_t(b), 1, [&](const uint8_t* p, size_t n) {
      if (eeg::raw(p, n, value)) ++rawCount;
      else if (n == 36 && eeg::metrics(p, n, decoded)) ++metricsCount;
    });
    assert(rawCount > 0 && metricsCount > 0 && historical.checksumErrors == 0);
    printf("Optional capture replay: raw=%u metrics=%u\n", rawCount, metricsCount);
  }
  puts("PASS: native EegCore corruption, bounds, wrap, timeout, reassembly and replay checks");
}
