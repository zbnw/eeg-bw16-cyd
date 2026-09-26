#pragma once
namespace board {
constexpr unsigned long kEegBaud = 57600;
constexpr unsigned long kBatchDelayMs = 24;
constexpr unsigned long kHealthIntervalMs = 1000;
// Set false only when keeping a pre-0.2 CYD receiver (types 1/2/3 unchanged).
constexpr bool kSendHealth = true;
constexpr bool kUsbDiagnostics = true;
}
