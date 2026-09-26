"""Incremental, bounded EEG decoder. Packet integrity does not imply no sample loss."""

MAX_PAYLOAD = 169
BAND_NAMES = ("delta", "theta", "low_alpha", "high_alpha", "low_beta", "high_beta", "low_gamma", "mid_gamma")


def decode_metrics(packet):
    payload = packet[3:-1]
    out = {}
    i = 0
    while i < len(payload):
        extended = 0
        while i < len(payload) and payload[i] == 0x55:
            extended += 1
            i += 1
        if i == len(payload):
            raise ValueError("truncated extended code")
        code = payload[i]
        i += 1
        size = 1
        if code >= 0x80:
            if i == len(payload):
                raise ValueError("missing field size")
            size = payload[i]
            i += 1
        if i + size > len(payload):
            raise ValueError("truncated field")
        value = payload[i:i + size]
        if not extended:
            if code in (2, 4, 5, 0x16):
                key, limit = {2: ("poor_signal", 200), 4: ("attention", 100),
                              5: ("meditation", 100), 0x16: ("blink", 255)}[code]
                if value[0] > limit:
                    raise ValueError("metric out of range")
                out[key] = value[0]
            elif code == 0x83:
                if size != 24:
                    raise ValueError("band field must contain 24 bytes")
                bands = [int.from_bytes(value[j:j + 3], "big") for j in range(0, 24, 3)]
                out["bands_raw"] = bands
                out["bands"] = dict(zip(("delta", "theta", "alpha", "beta", "gamma"),
                    (bands[0], bands[1], sum(bands[2:4]), sum(bands[4:6]), sum(bands[6:8]))))
        i += size
    return out


class StreamParser:
    def __init__(self):
        self.buffer = bytearray()
        self.checksum_errors = 0
        self.skipped_bytes = 0
        self.unsupported_packets = 0
        self.malformed_packets = 0
        self.raw_count = 0

    def feed(self, data):
        # Process bounded chunks so callers can safely replay large recordings.
        events = []
        for start in range(0, len(data), 4096):
            self.buffer.extend(data[start:start + 4096])
            cursor = 0
            while len(self.buffer) - cursor >= 3:
                if self.buffer[cursor:cursor + 2] != b"\xaa\xaa" or not 0 < self.buffer[cursor + 2] <= MAX_PAYLOAD:
                    cursor += 1
                    self.skipped_bytes += 1
                    continue
                length = self.buffer[cursor + 2] + 4
                if len(self.buffer) - cursor < length:
                    break
                packet = bytes(self.buffer[cursor:cursor + length])
                if sum(packet[3:]) & 255 != 255:
                    self.checksum_errors += 1
                    self.skipped_bytes += 1
                    cursor += 1
                    continue
                cursor += length
                if length == 8 and packet[3:5] == b"\x80\x02":
                    events.append({"type": "samples", "samples": [int.from_bytes(packet[5:7], "big", signed=True)],
                                   "sample_index": self.raw_count})
                    self.raw_count += 1
                elif length == 36:
                    try:
                        event = decode_metrics(packet)
                    except ValueError:
                        self.malformed_packets += 1
                    else:
                        events.append({"type": "metrics", "raw_index": self.raw_count, **event})
                else:
                    self.unsupported_packets += 1
            del self.buffer[:cursor]
        return events
