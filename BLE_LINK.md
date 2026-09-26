# BW16 to CYD BLE protocol, firmware 0.2.0

Updated 2026-09-25. Firmware sources and host tests have changed; hardware flashing is deferred to the user.
Read docs/09_v0.2升级与验收.md before upgrading.

Name: BW16-EEG
Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Notify: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E

Each notification is at most 20 bytes. Byte 0 is the type, byte 1 a wrapping 8-bit sequence shared by all types.

| Type | Bytes after sequence | Meaning |
| --- | --- | --- |
| 01 | 2..18, even | 1..9 signed int16 raw samples, big endian |
| 02 | 18 | First half of the original 36-byte metrics packet |
| 03 | 18 | Second half; must immediately follow type 02 |
| 04 | 18 | New sender health payload, schema 1 |

Health payload offsets (counting from notification byte 2):

| Offset | Length | Value |
| --- | --- | --- |
| 0 | 1 | Schema = 1 |
| 1..3 | 3 | Firmware major, minor, patch: 0,2,0 |
| 4..7 | 4 | Valid raw packets received by BW16 since boot |
| 8..11 | 4 | EEG UART checksum errors |
| 12..15 | 4 | Locally dropped notifications |
| 16..17 | 2 | Low 16 bits of incomplete-UART-packet timeout count |

All multibyte health integers are unsigned big endian. Health is sent roughly once per second.
Local drops include a full/reset queue and setData failure, not all over-air loss.
AmebaD notify() returns void; there is no end-to-end acknowledgement.

BW16 batches up to nine raw samples or waits at most 24 ms before enqueueing them.
A 32-message queue is drained no faster than one submission per 3 ms.
Metrics halves are enqueued as a pair; insufficient room discards both and advances their sequence numbers.
Parser recovery advances one byte on checksum failure. Unknown valid packet shapes are counted and ignored.

CYD queues 96 notifications and processes at most 16 per main-loop pass.
Duplicate sequences are discarded. A gap invalidates an incomplete metrics packet.
Intervening message types or a delay over 500 ms also invalidate the first half.
Reassembled metrics checksum and field layout are checked before commit.
Sequence gap counts represent events, not the number of missing samples; the 8-bit sequence cannot prove lossless capture.

The default CYD USB output remains binary raw/metrics packets at 115200.
The optional JSONL output exposes receiver and sender statistics; see web/PROTOCOL.md.
Healthy type 04 notifications keep the BLE session alive even when the EEG UART is quiet;
LCD LIVE still requires fresh raw samples. No valid BLE traffic for 8 s triggers reconnect.
Scans are synchronous, one second per attempt, with 3/6/12 s backoff. Connection calls can block longer.

## Upgrade compatibility

New CYD accepts old BW16 data but has no sender health fields.
New BW16 adds type 04, which old CYD treats as a framing error; raw/metrics formats are unchanged.
Upgrade CYD first. To retain an old CYD, set BW16 board::kSendHealth=false and rebuild.
Electrode, supply and UART pin assignments are unchanged by this release.

Historical hardware logs and captures are kept locally and are not part of the public source release.
They do not verify the current firmware's RF behavior, reconnection, electrical safety or signal accuracy.
