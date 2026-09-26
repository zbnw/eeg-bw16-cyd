# Sichiray EEG V6.1 Wiring

This document separates the working ESP32-CYD prototype from the planned BW16 PCB.
Do not copy GPIO numbers between the two designs.

## Confirmed module facts

The module is a Sichiray single-channel EEG V6.1. The manufacturer currently lists a
3.3 V input and a 3.3 V TTL UART with 9600 and 57600 baud options. This project uses
57600 baud, 8 data bits, no parity, and 1 stop bit.

With the component side facing up and `EEG V6.1` readable, the top-right pads are:

1. `VCC`
2. `RX`
3. `TX`
4. `GND`

The bottom pads, from left to right in the same orientation, are:

1. Reference electrode
2. Forehead electrode
3. Forehead cable shield
4. Reference ground electrode

The reference ground electrode is a patient electrode input. It is not digital GND.
Do not connect the reference ground or forehead shield to UART/system GND unless a
future manufacturer document explicitly requires it.

Manufacturer reference:
https://www.sichiray.com/eeg

## ESP32-CYD prototype

| EEG V6.1 | ESP32-CYD |
| --- | --- |
| `TX` | GPIO27 / UART2 RX |
| `GND` | GND |
| `RX` | Not connected |
| `VCC` | Local regulated 3.3 V supply |

The current firmware configures no UART TX pin. GPIO22 remains available only as an
unconnected future option.

## BW16 baseboard

The requested final inter-domain connection is exactly two nets:

| EEG V6.1 | BW16 bare module |
| --- | --- |
| `TX` | Pin 15, `PB2 / LP_UART_RXD` |
| `GND` | Pin 9, system GND |

EEG `VCC` and `RX` do not connect to BW16. EEG `VCC` is powered by the EEG battery
domain only. A series resistor is not required. A default-closed solder bridge or 0 ohm
footprint may be reserved for rework, but a direct copper connection is valid.

The BW16 pin mapping is from the Ai-Thinker BW16 V1.2.2 specification:
https://docs.ai-thinker.com/_media/bw16_v1.2.2_product_specification_en.pdf

## Meaning of independent battery power

The two VCC rails remain separate, but the battery negatives become electrically common
through the UART GND connection. This is independent supply with common-ground UART;
it is not galvanic isolation.

If the EEG battery is a single lithium cell, its maximum voltage can exceed 3.3 V. Do
not connect the cell directly to EEG `VCC`; use a regulator suitable for the real battery
voltage range.

## Bring-up order

1. With power off, confirm the two VCC rails are not connected.
2. Confirm EEG `RX` is not connected and all four electrode nets are isolated from GND.
3. Power EEG alone and verify 3.3 V between EEG `VCC` and `GND`.
4. Measure EEG `TX` relative to EEG `GND`; confirm 3.3 V logic activity.
5. Power the MCU side from its battery, then connect common GND and EEG TX.
6. Receive at 57600 8N1 and verify checksum/error counters before wearing electrodes.

See `hardware/SAFETY.md` before any on-body test.
