#include <Arduino.h>
#include <atomic>
#include <EegCore.h>
#include "BLEDevice.h"
#include "board_config.h"

// EEG TX -> PB2/D5, digital GND -> GND. Electrode/power wiring is unchanged.
namespace {
BLEService eegService(eeg::kServiceUuid);
BLECharacteristic eegNotify(eeg::kNotifyUuid);
BLEAdvertData advert, scanResponse;
std::atomic<bool> notificationsEnabled(false);
std::atomic<uint32_t> subscriptionGeneration(0);
uint32_t seenGeneration = 0;
bool wasReady = false;
eeg::Parser parser;
uint8_t rawBatch[18];
size_t rawBatchLength = 0;
uint32_t firstRawMs = 0, lastLogMs = 0, lastHealthMs = 0, lastSendMs = 0;
uint32_t rawCount = 0, metricsCount = 0, unsupportedCount = 0, droppedNotifications = 0;
uint8_t sequence = 0;
struct Message { uint8_t length; uint8_t data[20]; };
Message txQueue[32];
size_t txHead = 0, txUsed = 0;

bool ready() { return BLE.connected(0) && notificationsEnabled.load(); }
void notificationChanged(BLECharacteristic*, uint8_t, uint16_t cccd) {
  notificationsEnabled.store((cccd & GATT_CLIENT_CHAR_CONFIG_NOTIFY) != 0);
  ++subscriptionGeneration;
}
void resetTransport() {
  droppedNotifications += txUsed;
  txHead = txUsed = rawBatchLength = 0;
  sequence = 0;
}
void enqueue(uint8_t type, const uint8_t* data, size_t length) {
  if (!ready()) return;
  const uint8_t next = sequence++;
  if (length > 18 || txUsed == 32) { ++droppedNotifications; return; }
  Message& m = txQueue[(txHead + txUsed++) % 32];
  m.length = length + 2; m.data[0] = type; m.data[1] = next;
  memcpy(m.data + 2, data, length);
}
void flushRaw() {
  if (rawBatchLength) enqueue(1, rawBatch, rawBatchLength);
  rawBatchLength = 0;
}
void handlePacket(const uint8_t* bytes, size_t length) {
  int16_t value;
  if (eeg::raw(bytes, length, value)) {
    ++rawCount;
    if (!ready()) return;
    if (!rawBatchLength) firstRawMs = millis();
    rawBatch[rawBatchLength++] = bytes[5];
    rawBatch[rawBatchLength++] = bytes[6];
    if (rawBatchLength == sizeof(rawBatch)) flushRaw();
  } else if (length == 36) {
    eeg::Metrics metrics;
    if (!eeg::metrics(bytes, length, metrics)) { ++unsupportedCount; return; }
    ++metricsCount;
    flushRaw();
    if (!ready()) return;
    if (txUsed > 30) { sequence += 2; droppedNotifications += 2; return; }
    enqueue(2, bytes, 18);
    enqueue(3, bytes + 18, 18);
  } else ++unsupportedCount;
}
void sendQueued(uint32_t now) {
  if (!txUsed || !ready() || uint32_t(now - lastSendMs) < 3) return;
  lastSendMs = now;
  Message& m = txQueue[txHead];
  if (eegNotify.setData(m.data, m.length)) {
    // AmebaD notify() returns void: this is an ATT submission, NOT an ACK.
    eegNotify.notify(0);
  } else ++droppedNotifications;
  txHead = (txHead + 1) % 32; --txUsed;
}
void sendHealth() {
  uint8_t p[18] = {1, 0, 2, 0};
  eeg::write32(p + 4, rawCount);
  eeg::write32(p + 8, parser.checksumErrors);
  eeg::write32(p + 12, droppedNotifications);
  p[16] = parser.timeouts >> 8; p[17] = parser.timeouts;
  enqueue(4, p, sizeof(p));
}
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(board::kEegBaud, SERIAL_8N1);
  advert.addFlags(GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED);
  advert.addCompleteName("BW16-EEG");
  scanResponse.addCompleteServices(BLEUUID(eeg::kServiceUuid));
  eegNotify.setNotifyProperty(true);
  eegNotify.setCCCDCallback(notificationChanged);
  eegNotify.setBufferLen(20);
  eegService.addCharacteristic(eegNotify);
  BLE.init();
  BLE.configAdvert()->setAdvData(advert);
  BLE.configAdvert()->setScanRspData(scanResponse);
  BLE.configServer(1);
  BLE.addService(eegService);
  BLE.beginPeripheral();
  if (board::kUsbDiagnostics) Serial.println("BW16-EEG 0.2.0 BLE advertising");
}

void loop() {
  const uint32_t generation = subscriptionGeneration.load();
  const bool online = ready();
  if (generation != seenGeneration || online != wasReady) {
    resetTransport(); seenGeneration = generation; wasReady = online;
  }
  for (size_t budget = 0; budget < 512 && Serial1.available(); ++budget)
    parser.feed(uint8_t(Serial1.read()), millis(), handlePacket);
  const uint32_t now = millis();
  if (rawBatchLength && uint32_t(now - firstRawMs) >= board::kBatchDelayMs) flushRaw();
  if (board::kSendHealth && online && uint32_t(now - lastHealthMs) >= board::kHealthIntervalMs) {
    lastHealthMs = now; sendHealth();
  }
  if (subscriptionGeneration.load() == seenGeneration) sendQueued(now);
  if (board::kUsbDiagnostics && uint32_t(now - lastLogMs) >= 1000) {
    lastLogMs = now;
    Serial.print("v=0.2.0 ble="); Serial.print(online);
    Serial.print(" raw="); Serial.print(rawCount);
    Serial.print(" metrics="); Serial.print(metricsCount);
    Serial.print(" checksum="); Serial.print(parser.checksumErrors);
    Serial.print(" skipped="); Serial.print(parser.skippedBytes);
    Serial.print(" timeout="); Serial.print(parser.timeouts);
    Serial.print(" txdrop="); Serial.print(droppedNotifications);
    Serial.print(" unsupported="); Serial.println(unsupportedCount);
  }
  delay(1);
}
