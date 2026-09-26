# BW16 plus Sichiray EEG V6.1 Baseboard

## Scope

This is an EDA-ready connection specification for a carrier PCB containing a bare BW16
module and a Sichiray EEG V6.1 module. It is not a full custom EEG analog front end and
does not replace either module.

The EEG side has its own battery-derived 3.3 V rail. The BW16 side has its own supply.
Only UART TX and common GND cross between the domains.

## Required netlist

| From | Pin | Net | To |
| --- | --- | --- | --- |
| EEG module | `VCC` | `EEG_3V3` | EEG regulator output only |
| EEG module | `RX` | `NC_EEG_RX` | No connect |
| EEG module | `TX` | `EEG_UART_TX` | BW16 pin 15 `PB2 / LP_UART_RXD` |
| EEG module | `GND` | `EEG_GND` | EEG battery negative and ground NetTie |
| Ground NetTie | 1 | `EEG_GND` | EEG domain |
| Ground NetTie | 2 | `SYS_GND` | BW16 domain |
| BW16 | Pin 8 `VDD_3V3` | `SYS_3V3` | BW16 regulator output only |
| BW16 | Pin 9 `GND` | `SYS_GND` | System ground |
| BW16 | Pin 15 `PB2` | `EEG_UART_TX` | EEG TX |
| BW16 | Pin 16 `PB1` | `NC_BW16_TX1` | No connect to EEG |
| BW16 | Pin 3 `CHIP_EN` | `BW16_EN` | Pull-up/reset circuit |

BW16 pin references are for the bare 16-pad module, not the BW16-Kit development board.
Verify the selected library footprint against the Ai-Thinker V1.2.2 datasheet before layout.

## Electrode connector netlist

With the EEG module front side up and its text readable, the bottom pads are left-to-right:

| EEG pad | Net | Connector destination |
| --- | --- | --- |
| Reference electrode | `EEG_REF_ELECTRODE` | Reference electrode contact |
| Forehead electrode | `EEG_FOREHEAD_ELECTRODE` | Forehead sensing contact |
| Forehead shield | `EEG_FOREHEAD_SHIELD` | Forehead cable shield |
| Reference ground | `EEG_REF_GROUND` | Reference ground electrode contact |

Do not add GND symbols to these four nets. Do not join them to `EEG_GND`, `SYS_GND`, a
shield can, USB shell, or mounting holes.

## Power domains

```text
EEG battery -> EEG power switch -> 3.3 V regulator -> EEG_3V3 -> EEG VCC
EEG battery negative -------------------------------> EEG_GND

System battery -> system regulator -> SYS_3V3 -> BW16 VDD_3V3
System battery negative -----------------------> SYS_GND

EEG_GND -- single NetTie -- SYS_GND
```

The two VCC rails must not be connected. A single-cell lithium battery is above 3.3 V when
charged, so it requires regulation before EEG VCC. Select the actual regulator only after the
battery chemistry, minimum voltage, charging arrangement, and required run time are fixed.

At the EEG module, reserve local decoupling footprints for 100 nF plus 4.7-10 uF between
`EEG_3V3` and `EEG_GND`. Final values must remain compatible with the selected regulator.

## UART connection

Direct copper from EEG TX to BW16 RX is allowed because both interfaces are 3.3 V TTL.
A resistor is not required. For board-rework flexibility, an optional normally closed solder
bridge or 0 ohm footprint may be placed in series without making it a functional requirement.

UART settings:

```text
57600 baud, 8 data bits, no parity, 1 stop bit
```

Do not route BW16 PB1/TX1 to EEG RX in this revision.

## BW16 support circuits

- Pull `CHIP_EN` up to `SYS_3V3` and provide a reset-to-ground control.
- Break out `PA8 / UART_LOG_RXD`, `PA7 / UART_LOG_TXD`, `CHIP_EN`, `SYS_3V3`, and
  `SYS_GND` to a programming header or test pads.
- Keep the LP UART on PB2 dedicated to EEG reception.
- Add supply bulk and high-frequency decoupling close to BW16 VDD.
- Size the system regulator for Wi-Fi transmit current, not its idle current.

## Placement and routing

1. Put BW16 at a board edge with its antenna facing outward.
2. Follow the datasheet antenna keepout on every copper layer; no traces, planes, parts,
   battery, enclosure metal, or electrode wiring may enter that region.
3. Put the EEG module and electrode connector at the opposite end of the PCB.
4. Keep electrode nets away from the BW16 antenna, switch-node copper, clocks, UART, and
   high-current power paths.
5. Route `EEG_UART_TX` with a continuous nearby ground return. If split grounds are drawn,
   cross the boundary only next to the ground NetTie.
6. Join `EEG_GND` and `SYS_GND` at one documented point. Do not create a second join through
   mounting hardware, programmer cables, shields, or a charger.
7. Keep the two VCC copper regions visibly separate and use distinct net names everywhere.

## Test points

Provide small test points for:

- `EEG_3V3`
- `EEG_GND`
- `EEG_UART_TX`
- `SYS_3V3`
- `SYS_GND`
- `BW16_EN`
- `BW16_LOG_RXD`
- `BW16_LOG_TXD`

Avoid large test pads on the high-impedance electrode nets.

## ERC and design review checklist

- EEG `RX` and BW16 `PB1` carry explicit no-connect markers.
- EEG VCC has no path to SYS_3V3, including through connectors and test fixtures.
- `EEG_GND` and `SYS_GND` have exactly one intended NetTie connection.
- All four electrode nets are isolated from both digital grounds.
- Module footprints were checked with a 1:1 print and real modules.
- BW16 antenna keepout exists on every layer.
- Programming/charging connectors are inaccessible or disconnected during on-body use.
- Silkscreen identifies both battery polarities, module orientation, and non-medical status.

## Reference

Ai-Thinker BW16 V1.2.2 specification:
https://docs.ai-thinker.com/_media/bw16_v1.2.2_product_specification_en.pdf
