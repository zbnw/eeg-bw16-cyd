"""Capture unmodified CYD USB serial bytes to a binary file.

Requires pyserial: python -m pip install pyserial
This script is for data capture; it does not make any medical measurement.
"""

import argparse
import datetime
import json
import hashlib
import math
import pathlib
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Current CYD serial port, e.g. COMx on Windows")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle", type=float, default=1.5, help="Seconds to allow port-open reset/boot")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("--seconds must be positive")
    if not math.isfinite(args.settle) or args.settle < 0 or args.baud <= 0:
        parser.error("invalid settle time or baud")
    sidecar = args.out.with_suffix(args.out.suffix + ".json")
    if args.out.exists() or sidecar.exists():
        parser.error("capture or metadata file already exists; choose a new --out path")
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial missing: python -m pip install pyserial") from exc

    args.out.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    digest = hashlib.sha256()
    failure = None
    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    # Opening can still toggle pins in some drivers; the settling interval is explicit.
    with port:
        time.sleep(args.settle)
        port.write(b"binary\n")
        time.sleep(0.3)
        port.reset_input_buffer()
        with args.out.open("xb") as output:
            started_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
            start = time.monotonic()
            deadline = start + args.seconds
            try:
                while time.monotonic() < deadline:
                    port.timeout = min(0.2, max(0, deadline - time.monotonic()))
                    data = port.read(min(port.in_waiting or 1, 4096))
                    if data:
                        output.write(data)
                        digest.update(data)
                        total += len(data)
            except KeyboardInterrupt:
                failure = "interrupted"
            except (serial.SerialException, OSError) as exc:
                failure = str(exc)
            finally:
                elapsed = time.monotonic() - start
                with sidecar.open("x", encoding="utf-8") as metadata:
                    json.dump({"format": "eeg-binary", "port": args.port, "baud": args.baud,
                        "started_utc": started_utc,
                        "ended_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        "elapsed_seconds": elapsed, "requested_seconds": args.seconds,
                        "bytes": total, "sha256": digest.hexdigest(), "error": failure,
                        "timing": "host capture duration; samples have no device timestamps"}, metadata, indent=2)
    print(f"Saved {total} raw bytes in {elapsed:.2f} s to {args.out}")
    print(f"Capture metadata: {sidecar}")
    if failure:
        raise SystemExit(f"Partial capture preserved: {failure}")


if __name__ == "__main__":
    main()
