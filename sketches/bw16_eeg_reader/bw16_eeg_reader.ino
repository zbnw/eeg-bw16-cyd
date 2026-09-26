#include <Arduino.h>

// BW16 PB2 (D5) receives the EEG module's 57600 8N1 UART stream.
// USB/log UART forwards the exact bytes at 115200 8N1.
// PB1 is initialized by the core but is not used to transmit to EEG RX.
// Disconnect USB and other wired equipment before electrodes touch a person.
void setup() {
  Serial.begin(115200);
  Serial1.begin(57600, SERIAL_8N1);
}

void loop() {
  uint8_t bytes[128];
  size_t used = 0;
  while (Serial1.available() && used < sizeof(bytes)) {
    int value = Serial1.read();
    if (value >= 0) bytes[used++] = (uint8_t)value;
  }
  if (used) Serial.write(bytes, used);
  else delay(1);
}
