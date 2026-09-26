#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <SPI.h>
#include <atomic>
#include <EegCore.h>
#include <esp_arduino_version.h>
#include "board_config.h"

namespace {
constexpr int kTftMosi = board::kTftMosiPin;
constexpr int kTftSclk = board::kTftSclkPin;
constexpr int kTftCs = board::kTftCsPin;
constexpr int kTftDc = board::kTftDcPin;
constexpr int kTftRst = board::kTftResetPin;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kWaveTop = 18;
constexpr int kWaveBottom = 216;
constexpr int kWaveHeight = kWaveBottom - kWaveTop;
constexpr int kWaveMid = (kWaveTop + kWaveBottom) / 2;
constexpr uint16_t kColorBg = 0x0000;
constexpr uint16_t kColorGrid = 0x2104;
constexpr uint16_t kColorAxis = 0x7BEF;
constexpr uint16_t kColorWave = 0x07FF;
constexpr uint16_t kColorAttention = 0xFD20;
constexpr uint16_t kColorMeditation = 0x07E0;
constexpr uint16_t kColorBlink = 0xF800;
constexpr unsigned long kStatusRefreshMs = 250;
constexpr unsigned long kMetricsTimeoutMs = 3000;
constexpr unsigned long kConnectionTimeoutMs = 2000;
SPIClass tftSpi(VSPI);
HardwareSerial eegSerial(2);
struct BleMessage { uint8_t length; uint8_t data[20]; };
QueueHandle_t bleMessages = nullptr;
BLEClient* bleClient = nullptr;
BLEScan* bleScan = nullptr;
std::atomic<bool> bleLinkUp(false), bleDisconnected(false);
std::atomic<uint32_t> bleQueueDrops(0);
eeg::Parser uartParser;
eeg::BleAssembler assembler;
uint32_t lastBleScanMs = 0, lastBleDataMs = 0, scanDelayMs = 0;
uint32_t reconnectCount = 0, rawPacketCount = 0, bandPacketCount = 0;
uint32_t framingErrorCount = 0, unsupportedPacketCount = 0;
uint32_t lastStatusDrawMs = 0, lastRawPacketMs = 0, lastMetricsPacketMs = 0;
uint32_t lastRateCount = 0, lastRateMs = 0, rawRate = 0;
uint32_t peerRaw = 0, peerChecksum = 0, peerDrops = 0, peerTimeouts = 0, peerHealthMs = 0;
char peerVersion[20] = "unknown";
int attentionValue = -1, meditationValue = -1, poorSignalValue = -1;
int waveX = 0, prevWaveY = kWaveMid;
bool hasPrevPoint = false, resetWavePending = false;
constexpr size_t kWaveHistorySize = 1024;
constexpr uint8_t kSamplesPerColumn = 3;
int16_t waveHistory[kWaveHistorySize];
size_t waveHistoryUsed = 0, waveHistoryNext = 0;
int16_t columnLow = 32767, columnHigh = -32768, columnLast = 0;
uint8_t columnSamples = 0;
int32_t scaleLow = -100, scaleHigh = 100;
uint32_t lastScaleMs = 0;
bool jsonOutput = board::kDefaultJsonOutput;
int16_t jsonSamples[32];
size_t jsonUsed = 0;
uint32_t jsonFirstMs = 0, jsonFirstIndex = 0, jsonSeq = 0, lastJsonStatusMs = 0;
uint32_t bootId = 0, outputSession = 0;
char command[24];
size_t commandUsed = 0;
bool commandOverflow = false;

void writeCmd(uint8_t cmd) {
  digitalWrite(kTftDc, LOW);
  digitalWrite(kTftCs, LOW);
  tftSpi.transfer(cmd);
  digitalWrite(kTftCs, HIGH);
}

void writeData8(uint8_t data) {
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  tftSpi.transfer(data);
  digitalWrite(kTftCs, HIGH);
}

void writeData16(uint16_t data) {
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  tftSpi.transfer(data >> 8);
  tftSpi.transfer(data & 0xFF);
  digitalWrite(kTftCs, HIGH);
}

void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCmd(0x2A);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  tftSpi.transfer(x0 >> 8);
  tftSpi.transfer(x0 & 0xFF);
  tftSpi.transfer(x1 >> 8);
  tftSpi.transfer(x1 & 0xFF);
  digitalWrite(kTftCs, HIGH);

  writeCmd(0x2B);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  tftSpi.transfer(y0 >> 8);
  tftSpi.transfer(y0 & 0xFF);
  tftSpi.transfer(y1 >> 8);
  tftSpi.transfer(y1 & 0xFF);
  digitalWrite(kTftCs, HIGH);

  writeCmd(0x2C);
}

void drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) {
    return;
  }
  setAddrWindow(x, y, x, y);
  writeData16(color);
}

void drawFastVLine(int x, int y, int h, uint16_t color) {
  if (x < 0 || x >= kScreenWidth || h <= 0) {
    return;
  }
  const int y0 = max(0, y);
  const int y1 = min(kScreenHeight - 1, y + h - 1);
  if (y1 < y0) {
    return;
  }
  setAddrWindow(x, y0, x, y1);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  for (int yy = y0; yy <= y1; ++yy) {
    tftSpi.transfer(color >> 8);
    tftSpi.transfer(color & 0xFF);
  }
  digitalWrite(kTftCs, HIGH);
}

void drawFastHLine(int x, int y, int w, uint16_t color) {
  if (y < 0 || y >= kScreenHeight || w <= 0) {
    return;
  }
  const int x0 = max(0, x);
  const int x1 = min(kScreenWidth - 1, x + w - 1);
  if (x1 < x0) {
    return;
  }
  setAddrWindow(x0, y, x1, y);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  for (int xx = x0; xx <= x1; ++xx) {
    tftSpi.transfer(color >> 8);
    tftSpi.transfer(color & 0xFF);
  }
  digitalWrite(kTftCs, HIGH);
}

void fillRect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const int x0 = max(0, x);
  const int y0 = max(0, y);
  const int x1 = min(kScreenWidth - 1, x + w - 1);
  const int y1 = min(kScreenHeight - 1, y + h - 1);
  if (x1 < x0 || y1 < y0) {
    return;
  }
  setAddrWindow(x0, y0, x1, y1);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(kTftCs, LOW);
  for (int yy = y0; yy <= y1; ++yy) {
    for (int xx = x0; xx <= x1; ++xx) {
      tftSpi.transfer(color >> 8);
      tftSpi.transfer(color & 0xFF);
    }
  }
  digitalWrite(kTftCs, HIGH);
}

void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
  const int dx = abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void ili9341InitLandscape() {
  pinMode(kTftCs, OUTPUT);
  pinMode(kTftDc, OUTPUT);
  pinMode(kTftRst, OUTPUT);
  pinMode(board::kBacklightPin, OUTPUT);

  digitalWrite(kTftCs, HIGH);
  digitalWrite(kTftDc, HIGH);
  digitalWrite(board::kBacklightPin, HIGH);

  tftSpi.begin(kTftSclk, -1, kTftMosi, kTftCs);
  tftSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));

  digitalWrite(kTftRst, HIGH);
  delay(20);
  digitalWrite(kTftRst, LOW);
  delay(20);
  digitalWrite(kTftRst, HIGH);
  delay(120);

  writeCmd(0x01);
  delay(5);
  writeCmd(0x28);

  writeCmd(0xCF); writeData8(0x00); writeData8(0xC1); writeData8(0x30);
  writeCmd(0xED); writeData8(0x64); writeData8(0x03); writeData8(0x12); writeData8(0x81);
  writeCmd(0xE8); writeData8(0x85); writeData8(0x00); writeData8(0x78);
  writeCmd(0xCB); writeData8(0x39); writeData8(0x2C); writeData8(0x00); writeData8(0x34); writeData8(0x02);
  writeCmd(0xF7); writeData8(0x20);
  writeCmd(0xEA); writeData8(0x00); writeData8(0x00);
  writeCmd(0xC0); writeData8(0x23);
  writeCmd(0xC1); writeData8(0x10);
  writeCmd(0xC5); writeData8(0x3E); writeData8(0x28);
  writeCmd(0xC7); writeData8(0x86);
  writeCmd(0x36); writeData8(0x28);
  writeCmd(0x3A); writeData8(0x55);
  writeCmd(0xB1); writeData8(0x00); writeData8(0x18);
  writeCmd(0xB6); writeData8(0x08); writeData8(0x82); writeData8(0x27);
  writeCmd(0xF2); writeData8(0x00);
  writeCmd(0x26); writeData8(0x01);
  writeCmd(0xE0);
  const uint8_t gammaPos[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
  for (uint8_t v : gammaPos) writeData8(v);
  writeCmd(0xE1);
  const uint8_t gammaNeg[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
  for (uint8_t v : gammaNeg) writeData8(v);

  writeCmd(0x11);
  delay(120);
  writeCmd(0x29);
  delay(20);
}

const uint8_t* glyph(char c) {
  static const uint8_t blank[7] = {};
  static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}
  };
  if (c >= '0' && c <= '9') return digits[c - '0'];
  switch (c) {
    case 'A': { static const uint8_t v[7]={14,17,17,31,17,17,17}; return v; }
    case 'B': { static const uint8_t v[7]={30,17,17,30,17,17,30}; return v; }
    case 'C': { static const uint8_t v[7]={14,17,16,16,16,17,14}; return v; }
    case 'D': { static const uint8_t v[7]={30,17,17,17,17,17,30}; return v; }
    case 'E': { static const uint8_t v[7]={31,16,16,30,16,16,31}; return v; }
    case '.': { static const uint8_t v[7]={0,0,0,0,0,6,6}; return v; }
    case ':': { static const uint8_t v[7]={0,6,6,0,6,6,0}; return v; }
    case 'U': { static const uint8_t v[7]={17,17,17,17,17,17,14}; return v; }
    case 'J': { static const uint8_t v[7]={7,2,2,2,18,18,12}; return v; }
    case 'Y': { static const uint8_t v[7]={17,17,10,4,4,4,4}; return v; }
    case 'M': { static const uint8_t v[7]={17,27,21,21,17,17,17}; return v; }
    case 'P': { static const uint8_t v[7]={30,17,17,30,16,16,16}; return v; }
    case 'Q': { static const uint8_t v[7]={14,17,17,17,21,18,13}; return v; }
    case 'G': { static const uint8_t v[7]={14,17,16,23,17,17,14}; return v; }
    case 'H': { static const uint8_t v[7]={17,17,17,31,17,17,17}; return v; }
    case 'I': { static const uint8_t v[7]={31,4,4,4,4,4,31}; return v; }
    case 'L': { static const uint8_t v[7]={16,16,16,16,16,16,31}; return v; }
    case 'N': { static const uint8_t v[7]={17,25,21,19,17,17,17}; return v; }
    case 'O': { static const uint8_t v[7]={14,17,17,17,17,17,14}; return v; }
    case 'R': { static const uint8_t v[7]={30,17,17,30,20,18,17}; return v; }
    case 'S': { static const uint8_t v[7]={15,16,16,14,1,1,30}; return v; }
    case 'T': { static const uint8_t v[7]={31,4,4,4,4,4,4}; return v; }
    case 'V': { static const uint8_t v[7]={17,17,17,17,17,10,4}; return v; }
    case 'W': { static const uint8_t v[7]={17,17,17,21,21,21,10}; return v; }
    case '/': { static const uint8_t v[7]={1,2,2,4,8,8,16}; return v; }
    case '-': { static const uint8_t v[7]={0,0,0,31,0,0,0}; return v; }
    default: return blank;
  }
}

void drawText(int x, int y, const char* value, uint16_t color) {
  for (; *value && x + 5 < kScreenWidth; ++value, x += 6) {
    const uint8_t* rows = glyph(*value);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row] & (1 << (4 - col))) drawPixel(x + col, y + row, color);
      }
    }
  }
}

void drawStaticUi() {
  fillRect(0, 0, kScreenWidth, kScreenHeight, kColorBg);
  for (int x = 0; x < kScreenWidth; x += 40) {
    drawFastVLine(x, 0, kScreenHeight, kColorGrid);
  }
  for (int y = kWaveTop; y <= kWaveBottom; y += 40) {
    drawFastHLine(0, y, kScreenWidth, kColorGrid);
  }
  drawFastHLine(0, kWaveTop, kScreenWidth, kColorAxis);
  drawFastHLine(0, kWaveBottom, kScreenWidth, kColorAxis);
  fillRect(0, 220, kScreenWidth, 20, 0x0841);
  waveX = 0;
  prevWaveY = kWaveMid;
  hasPrevPoint = false;
}

void flushJsonSamples() {
  if (!jsonOutput || !jsonUsed) return;
  char line[512];
  int n = snprintf(line, sizeof(line),
      "{\"v\":1,\"type\":\"samples\",\"seq\":%lu,\"device_rx_ms\":%lu,\"sample_index\":%lu,\"samples\":[",
      (unsigned long)jsonSeq++, (unsigned long)jsonFirstMs, (unsigned long)jsonFirstIndex);
  for (size_t i = 0; i < jsonUsed; ++i)
    n += snprintf(line + n, sizeof(line) - n, "%s%d", i ? "," : "", jsonSamples[i]);
  snprintf(line + n, sizeof(line) - n, "]}");
  Serial.println(line);
  jsonUsed = 0;
}

void sendHello() {
  Serial.printf("{\"v\":1,\"type\":\"hello\",\"device_id\":\"CYD-%llX\",\"firmware\":\"%s\",\"session_id\":\"%08lX-%lu\",\"source\":\"%s\"}\n",
                ESP.getEfuseMac(), eeg::kVersion, (unsigned long)bootId,
                (unsigned long)outputSession, board::kUseBleEeg ? "ble" : "uart");
}

void sendJsonStatus() {
  if (!jsonOutput) return;
  flushJsonSamples();
  const uint32_t now = millis();
  Serial.printf("{\"v\":1,\"type\":\"status\",\"seq\":%lu,\"uptime_ms\":%lu,\"connected\":%s,\"received_rate_hz\":%lu,\"raw_count\":%lu,\"metrics_count\":%lu,\"ble_gap_events\":%lu,\"ble_queue_drops\":%lu,\"ble_malformed\":%lu,\"checksum_errors\":%lu,\"frame_errors\":%lu,\"reconnects\":%lu,\"uart_skipped_bytes\":%lu,\"uart_timeouts\":%lu,\"free_heap\":%lu",
      (unsigned long)jsonSeq++, (unsigned long)now,
      (board::kUseBleEeg ? bleLinkUp.load() : (lastRawPacketMs && now - lastRawPacketMs < 2000)) ? "true" : "false",
      (unsigned long)rawRate, (unsigned long)rawPacketCount, (unsigned long)bandPacketCount,
      (unsigned long)assembler.gapEvents, (unsigned long)bleQueueDrops.load(),
      (unsigned long)assembler.malformed, (unsigned long)uartParser.checksumErrors,
      (unsigned long)framingErrorCount, (unsigned long)reconnectCount,
      (unsigned long)uartParser.skippedBytes, (unsigned long)uartParser.timeouts,
      (unsigned long)ESP.getFreeHeap());
  if (peerHealthMs && now - peerHealthMs < 3000)
    Serial.printf(",\"bw16_firmware\":\"%s\",\"bw16_raw_count\":%lu,\"bw16_checksum_errors\":%lu,\"bw16_tx_drops\":%lu,\"bw16_uart_timeouts\":%lu",
        peerVersion, (unsigned long)peerRaw, (unsigned long)peerChecksum,
        (unsigned long)peerDrops, (unsigned long)peerTimeouts);
  Serial.println("}");
}

void consumeCommands() {
  for (size_t budget = 0; budget < 64 && Serial.available(); ++budget) {
    const char c = char(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      command[commandUsed] = 0;
      if (!commandOverflow) {
        if (strcmp(command, "json") == 0) {
          flushJsonSamples(); jsonOutput = true; jsonSeq = 0; ++outputSession; sendHello();
        } else if (strcmp(command, "binary") == 0) {
          flushJsonSamples(); jsonOutput = false; jsonUsed = 0;
        } else if (strcmp(command, "status") == 0 && jsonOutput) sendJsonStatus();
      }
      commandUsed = 0; commandOverflow = false;
    } else if (commandUsed < sizeof(command) - 1) command[commandUsed++] = c;
    else commandOverflow = true;
  }
}

void drawStatusBars() {
  const uint32_t now = millis();
  const bool live = lastRawPacketMs && now - lastRawPacketMs <= kConnectionTimeoutMs;
  const bool fresh = lastMetricsPacketMs && now - lastMetricsPacketMs <= kMetricsTimeoutMs;
  if (!lastRateMs) { lastRateMs = now; lastRateCount = rawPacketCount; }
  if (now - lastRateMs >= 1000) {
    rawRate = uint32_t(uint64_t(rawPacketCount - lastRateCount) * 1000 / (now - lastRateMs));
    lastRateCount = rawPacketCount; lastRateMs = now;
  }
  char top[54], bottom[54], detail[54];
  snprintf(top, sizeof(top), "%s %s %lu/S  V%s",
      board::kUseBleEeg ? "BLE" : "UART",
      board::kUseBleEeg && !bleLinkUp.load() ? "SEARCH" : (live ? "LIVE" : "NO DATA"),
      (unsigned long)rawRate, eeg::kVersion);
  if (!fresh || poorSignalValue < 0) snprintf(bottom, sizeof(bottom), "SIGNAL --");
  else snprintf(bottom, sizeof(bottom), "SIGNAL %d %s A%d M%d", poorSignalValue,
      poorSignalValue == 200 ? "NO CONTACT" : (poorSignalValue == 0 ? "GOOD" : "NOISY"),
      attentionValue, meditationValue);
  snprintf(detail, sizeof(detail), "G%lu Q%lu E%lu R%lu %s",
      (unsigned long)assembler.gapEvents, (unsigned long)bleQueueDrops.load(),
      (unsigned long)(framingErrorCount + uartParser.checksumErrors + assembler.malformed),
      (unsigned long)reconnectCount, jsonOutput ? "JSON" : "BIN");
  static char oldTop[54] = "", oldBottom[54] = "", oldDetail[54] = "";
  if (strcmp(top, oldTop)) {
    fillRect(0, 0, kScreenWidth, kWaveTop, kColorBg);
    drawText(4, 5, top, live ? kColorWave : kColorAttention);
    strcpy(oldTop, top);
  }
  if (strcmp(bottom, oldBottom)) {
    fillRect(0, 220, kScreenWidth, 9, 0x0841);
    drawText(4, 221, bottom, fresh && poorSignalValue == 0 ? kColorWave : kColorAttention);
    strcpy(oldBottom, bottom);
  }
  if (strcmp(detail, oldDetail)) {
    fillRect(0, 230, kScreenWidth, 10, 0x0841);
    drawText(4, 231, detail, kColorAxis);
    strcpy(oldDetail, detail);
  }
}

void restoreWaveColumn(int x) {
  const uint16_t columnColor = (x % 40) == 0 ? kColorGrid : kColorBg;
  drawFastVLine(x, kWaveTop + 1, kWaveHeight - 1, columnColor);
  for (int y = kWaveTop + 40; y < kWaveBottom; y += 40) drawPixel(x, y, kColorGrid);
}

void pushWaveSample(int16_t rawData) {
  ++rawPacketCount;
  const unsigned long now = millis();
  if (resetWavePending || (lastRawPacketMs != 0 && now - lastRawPacketMs > 1000)) {
    resetWavePending = false;
    waveHistoryUsed = 0;
    waveHistoryNext = 0;
    columnSamples = 0;
    columnLow = 32767;
    columnHigh = -32768;
    hasPrevPoint = false;
  }
  lastRawPacketMs = now;
  waveHistory[waveHistoryNext] = rawData;
  waveHistoryNext = (waveHistoryNext + 1) % kWaveHistorySize;
  if (waveHistoryUsed < kWaveHistorySize) ++waveHistoryUsed;
  if (rawData < columnLow) columnLow = rawData;
  if (rawData > columnHigh) columnHigh = rawData;
  columnLast = rawData;
  if (++columnSamples < kSamplesPerColumn) return;
  columnSamples = 0;

  if (waveHistoryUsed <= kSamplesPerColumn || now - lastScaleMs >= board::kScaleRefreshMs ||
      columnLow < scaleLow || columnHigh > scaleHigh) {
    lastScaleMs = now;
    int32_t low = 32767, high = -32768;
    for (size_t i = 0; i < waveHistoryUsed; ++i) {
      if (waveHistory[i] < low) low = waveHistory[i];
      if (waveHistory[i] > high) high = waveHistory[i];
    }
    const int32_t padding = max(int32_t(1), (high - low) / 20);
    low -= padding; high += padding;
    // Never join points mapped through different vertical scales.
    if (low != scaleLow || high != scaleHigh) hasPrevPoint = false;
    scaleLow = low; scaleHigh = high;
  }
  const int32_t low = scaleLow, high = scaleHigh;
  auto yFor = [&](int16_t value) -> int {
    const int32_t y = kWaveBottom - 2 -
        ((int32_t(value) - low) * (kWaveHeight - 4)) / (high - low);
    return constrain(int(y), kWaveTop + 2, kWaveBottom - 2);
  };
  int yTop = yFor(columnHigh);
  int yBottom = yFor(columnLow);
  const int lastY = yFor(columnLast);
  if (hasPrevPoint) {
    yTop = min(yTop, prevWaveY);
    yBottom = max(yBottom, prevWaveY);
  }
  restoreWaveColumn(waveX);
  drawFastVLine(waveX, yTop, yBottom - yTop + 1, kColorWave);
  hasPrevPoint = true;
  prevWaveY = lastY;
  columnLow = 32767;
  columnHigh = -32768;
  waveX = (waveX + 1) % kScreenWidth;
  if (waveX == 0) {
    hasPrevPoint = false;
  }
}

void outputMetrics(const eeg::Metrics& m) {
  flushJsonSamples();
  Serial.printf("{\"v\":1,\"type\":\"metrics\",\"seq\":%lu", (unsigned long)jsonSeq++);
  if (m.signal >= 0) Serial.printf(",\"poor_signal\":%d", m.signal);
  if (m.attention >= 0) Serial.printf(",\"attention\":%d", m.attention);
  if (m.meditation >= 0) Serial.printf(",\"meditation\":%d", m.meditation);
  if (m.hasBands) {
    Serial.print(",\"bands_raw\":[");
    for (size_t i = 0; i < 8; ++i) Serial.printf("%s%lu", i ? "," : "", (unsigned long)m.bands[i]);
    Serial.printf("],\"bands\":{\"delta\":%lu,\"theta\":%lu,\"alpha\":%lu,\"beta\":%lu,\"gamma\":%lu}",
        (unsigned long)m.bands[0], (unsigned long)m.bands[1],
        (unsigned long)(m.bands[2] + m.bands[3]), (unsigned long)(m.bands[4] + m.bands[5]),
        (unsigned long)(m.bands[6] + m.bands[7]));
  }
  Serial.println("}");
}

void handleEegPacket(const uint8_t* packet, size_t length) {
  if (board::kUseBleEeg && assembler.discontinuity) {
    assembler.discontinuity = false;
    flushJsonSamples(); resetWavePending = true;
  }
  int16_t value;
  if (eeg::raw(packet, length, value)) {
    if (jsonOutput) {
      if (!jsonUsed) { jsonFirstMs = millis(); jsonFirstIndex = rawPacketCount; }
      jsonSamples[jsonUsed++] = value;
      if (jsonUsed == 32) flushJsonSamples();
    } else Serial.write(packet, length);
    pushWaveSample(value);
  } else if (length == 36) {
    eeg::Metrics m;
    if (!eeg::metrics(packet, length, m)) { ++framingErrorCount; return; }
    poorSignalValue = m.signal; attentionValue = m.attention; meditationValue = m.meditation;
    ++bandPacketCount; lastMetricsPacketMs = millis();
    if (jsonOutput) outputMetrics(m); else Serial.write(packet, length);
  } else {
    ++unsupportedPacketCount;
    return;
  }
}

void onHealth(const uint8_t* p) {
  snprintf(peerVersion, sizeof(peerVersion), "%u.%u.%u", p[1], p[2], p[3]);
  peerRaw = eeg::read32(p + 4); peerChecksum = eeg::read32(p + 8);
  peerDrops = eeg::read32(p + 12); peerTimeouts = (uint16_t(p[16]) << 8) | p[17];
  peerHealthMs = millis();
}

void consumeEegSerial() {
  for (size_t budget = 0; budget < 512 && eegSerial.available(); ++budget)
    uartParser.feed(uint8_t(eegSerial.read()), millis(), handleEegPacket);
}

void onBleNotification(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (!bleMessages || length < 2 || length > 20) return;
  BleMessage message = {};
  message.length = length; memcpy(message.data, data, length);
  if (xQueueSend(bleMessages, &message, 0) != pdTRUE) ++bleQueueDrops;
}
class EegBleClientCallbacks : public BLEClientCallbacks {
 public:
  void onConnect(BLEClient*) override { bleLinkUp.store(true); }
  void onDisconnect(BLEClient*) override { bleLinkUp.store(false); bleDisconnected.store(true); }
};
EegBleClientCallbacks eegBleClientCallbacks;

void resetBleSession() {
  flushJsonSamples();
  xQueueReset(bleMessages); assembler.reset();
  resetWavePending = true; lastRawPacketMs = 0; lastMetricsPacketMs = 0; peerHealthMs = 0;
}
void consumeBleMessages() {
  BleMessage message;
  for (size_t budget = 0; budget < 16 && xQueueReceive(bleMessages, &message, 0) == pdTRUE; ++budget) {
    // Break the waveform before drawing a sample across an observed loss.
    // The assembler itself rejects duplicates and invalid metric pairs.
    if (assembler.accept(message.data, message.length, millis(), handleEegPacket, onHealth))
      lastBleDataMs = millis();
  }
}
void connectBleEeg() {
  if (!bleClient || !bleScan) return;
  const uint32_t now = millis();
  if (bleClient->isConnected()) {
    if (uint32_t(now - lastBleDataMs) >= board::kBleStallMs) {
      bleClient->disconnect();
    }
    return;
  }
  if (lastBleScanMs && now - lastBleScanMs < scanDelayMs) return;
  lastBleScanMs = now;
  // Keep the proven synchronous API, but limit each scan to one second.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  BLEScanResults* results = bleScan->start(1, false);
#else
  BLEScanResults scanResults = bleScan->start(1, false);
  BLEScanResults* results = &scanResults;
#endif
  if (!results) return;
  for (int i = 0; i < results->getCount(); ++i) {
    BLEAdvertisedDevice device = results->getDevice(i);
    if (!device.haveName() || device.getName() != "BW16-EEG") continue;
    if (!bleClient->connect(&device)) break;
    BLERemoteService* service = bleClient->getService(BLEUUID(eeg::kServiceUuid));
    if (!service) break;
    BLERemoteCharacteristic* characteristic = service->getCharacteristic(BLEUUID(eeg::kNotifyUuid));
    if (!characteristic || !characteristic->canNotify()) break;
    resetBleSession(); bleDisconnected.store(false);
    characteristic->registerForNotify(onBleNotification);
    lastBleDataMs = millis(); scanDelayMs = board::kBleRetryMs;
    bleLinkUp.store(bleClient->isConnected());
    bleScan->clearResults(); return;
  }
  if (bleClient->isConnected()) bleClient->disconnect();
  bleLinkUp.store(false); bleScan->clearResults();
  scanDelayMs = scanDelayMs ? min(uint32_t(12000), scanDelayMs * 2) : board::kBleRetryMs;
}
} // namespace

void setup() {
  Serial.begin(115200);
  bootId = esp_random();
  ili9341InitLandscape();
  drawStaticUi();
  drawStatusBars();
  if (jsonOutput) { ++outputSession; sendHello(); }
  if (board::kUseBleEeg) {
    bleMessages = xQueueCreate(board::kBleQueueLength, sizeof(BleMessage));
    if (!bleMessages) { drawText(4, 5, "BLE ERROR", kColorBlink); return; }
    BLEDevice::init("CYD-EEG");
    bleClient = BLEDevice::createClient();
    if (!bleClient) { drawText(4, 5, "BLE ERROR", kColorBlink); return; }
    bleClient->setClientCallbacks(&eegBleClientCallbacks);
    bleScan = BLEDevice::getScan();
    bleScan->setActiveScan(true); bleScan->setInterval(100); bleScan->setWindow(80);
  } else {
    eegSerial.setRxBufferSize(2048);
    eegSerial.begin(board::kEegBaud, SERIAL_8N1, board::kEegRxPin, board::kEegTxPin);
  }
}

void loop() {
  consumeCommands();
  if (board::kUseBleEeg && bleMessages) {
    if (bleDisconnected.exchange(false)) { ++reconnectCount; resetBleSession(); }
    consumeBleMessages(); connectBleEeg();
  } else if (!board::kUseBleEeg) consumeEegSerial();
  const uint32_t now = millis();
  if (jsonUsed && now - jsonFirstMs >= 100) flushJsonSamples();
  if (now - lastStatusDrawMs >= kStatusRefreshMs) {
    lastStatusDrawMs = now; drawStatusBars();
  }
  if (jsonOutput && now - lastJsonStatusMs >= 1000) {
    lastJsonStatusMs = now; sendJsonStatus();
  }
  delay(1);
}
