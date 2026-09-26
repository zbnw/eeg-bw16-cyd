# Prototype Safety Boundary

This project is a development prototype, not a medical device and not a diagnostic system.

## Mandatory rules for on-body use

- Power the worn EEG/BW16 assembly from batteries only while electrodes touch a person.
- Disconnect USB, UART programmers, debug probes, grounded oscilloscopes, bench supplies,
  chargers, and all other wired external equipment from the worn EEG/BW16 assembly before
  wearing the electrodes. A computer-connected ESP32-CYD may remain on USB only when its
  sole connection to the worn assembly is BLE radio, with no shared wire or ground.
- Do not charge either battery on the worn assembly while electrodes are worn.
- Inspect electrode cables and connectors before each test. Do not use damaged insulation or
  exposed conductors.
- Stop testing if the board becomes hot, resets repeatedly, smells unusual, or shows battery
  swelling or damage.

## Independent supply is not isolation

The selected direct UART design connects EEG TX and EEG GND to BW16 RX and system GND.
Therefore the two VCC rails are separate, but the systems share an electrical reference.
This is not galvanic isolation.

If a wired computer/debug connection is required during on-body use, redesign the interface
with a properly rated isolated data path and isolated power architecture. Do not assume that a
generic USB isolator or an optocoupler alone creates a medically safe system.

## Battery design

- Do not apply a raw lithium-cell voltage directly to the 3.3 V EEG VCC input.
- Use protected cells and a regulator appropriate for the full battery voltage range.
- Include polarity protection and a power switch.
- Keep charging and wearing as mutually exclusive operating states.

## Labels

The enclosure and PCB should state:

```text
PROTOTYPE - NOT FOR MEDICAL DIAGNOSIS
BATTERY OPERATION ONLY WHILE WORN
DISCONNECT USB/CHARGER BEFORE USE
```
