#pragma once

namespace board {
// Working ESP32-2432S028R / CYD display wiring.
constexpr int kTftMosiPin = 13;
constexpr int kTftSclkPin = 14;
constexpr int kTftCsPin = 15;
constexpr int kTftDcPin = 2;
constexpr int kTftResetPin = 4;
constexpr int kBacklightPin = 21;

// EEG module UART.
// Optional bench wiring: EEG TX -> ESP32 GPIO27 plus common GND.
// The wireless build receives EEG samples from the battery-powered BW16 via BLE.
constexpr bool kUseBleEeg = true;
constexpr int kEegRxPin = 27;
constexpr int kEegTxPin = -1;
constexpr int kOptionalEegTxPad = 22;
constexpr unsigned long kEegBaud = 57600;
constexpr bool kDefaultJsonOutput = false;
constexpr unsigned long kBleRetryMs = 3000;
constexpr unsigned long kBleStallMs = 8000;
constexpr unsigned long kScaleRefreshMs = 125;
constexpr size_t kBleQueueLength = 96;
}  // namespace board
