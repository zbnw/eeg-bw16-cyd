# Sichiray EEG V6.1 Integration Record

## Evidence used

- User-supplied front-side module image marked `EEG V6.1`.
- Manufacturer product page: https://www.sichiray.com/eeg
- Existing project packet captures summarized in `AGENTS.md`.

## Electrical interface

| Item | Confirmed value |
| --- | --- |
| Supply input | 3.3 V |
| UART logic | 3.3 V TTL |
| Project baud rate | 57600, 8N1 |
| Manufacturer-listed baud rates | 9600 and 57600 |
| Manufacturer-listed current | 18 mA |
| Channel count | 1 |

Do not infer a wider VCC range from the 3.3 V nominal value. The PCB power design must
keep EEG VCC at 3.3 V and must be checked on the assembled prototype.

## Pad identification

Front view means components face the observer and `EEG V6.1` is readable.

Top-right pads, top to bottom:

| Symbol pin | Name | PCB treatment |
| --- | --- | --- |
| E1 | `VCC` | EEG-local regulated 3.3 V |
| E2 | `RX` | No-connect marker |
| E3 | `TX` | Output to MCU UART RX |
| E4 | `GND` | EEG battery negative and UART reference |

Bottom pads, left to right:

| Symbol pin | Net name |
| --- | --- |
| E5 | `EEG_REF_ELECTRODE` |
| E6 | `EEG_FOREHEAD_ELECTRODE` |
| E7 | `EEG_FOREHEAD_SHIELD` |
| E8 | `EEG_REF_GROUND` |

The four electrode nets go only to the electrode connector. They are not interchangeable
with digital GND.

## Footprint constraint

The image establishes orientation and pad order, not pitch, hole size, board outline, or
component keepout. Measure the real module or obtain its mechanical drawing before making
the footprint. Print the footprint at 1:1 scale and physically place the module on it before
ordering the carrier PCB.

The footprint must mark:

- Component-side orientation.
- `VCC` and reference-electrode corners.
- Pin 1 on both symbol and footprint.
- Any module underside components or exposed conductors.

## Data stream

The current decoder expects:

```text
Raw:     AA AA 04 80 02 hi lo checksum
Metrics: AA AA 20 [32-byte payload] checksum
```

Checksum is the low byte of `0xFF - sum(payload)`. The current LCD uses a linear
min/max display scale without smoothing. Preserve original signed raw samples in
recordings; offline smoothing or artifact flags must stay separate from source bytes.
