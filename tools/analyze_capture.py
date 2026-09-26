"""Parse EEG V6.1 raw/metrics packets from an unmodified binary capture.

CSV time is optional and approximate: these packets have no sample timestamps.
Plots require matplotlib; basic parsing and CSV only need Python stdlib.
This analysis is not a medical or heart-rate measurement.
"""

import argparse
import hashlib
import csv
import json
import math
from pathlib import Path
from eeg_protocol import StreamParser


def parse(data, with_stats=False):
    parser = StreamParser()
    raw, metrics = [], []
    for start in range(0, len(data), 4096):
        for event in parser.feed(data[start:start + 4096]):
            if event["type"] == "samples":
                raw.extend(event["samples"])
            else:
                metrics.append({"raw_index": event["raw_index"], "signal": event.get("poor_signal"),
                                "bands": event.get("bands_raw"), "attention": event.get("attention"),
                                "meditation": event.get("meditation")})
    result = (raw, metrics, parser.checksum_errors, parser.skipped_bytes, len(parser.buffer))
    return result + (parser.unsupported_packets, parser.malformed_packets) if with_stats else result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--csv", type=Path, help="Write raw samples and metric-adjacent flags")
    parser.add_argument("--plot", type=Path, help="Write a PNG using matplotlib")
    parser.add_argument("--duration", type=float, help="Approximate elapsed capture seconds")
    args = parser.parse_args()
    if args.duration is not None and (not math.isfinite(args.duration) or args.duration <= 0):
        parser.error("--duration must be positive")
    metadata = {}
    sidecar = args.capture.with_suffix(args.capture.suffix + ".json")
    if sidecar.exists():
        metadata = json.loads(sidecar.read_text(encoding="utf-8"))
        if args.duration is None:
            duration = metadata.get("elapsed_seconds")
            if isinstance(duration, (int, float)) and math.isfinite(duration) and duration > 0:
                args.duration = duration
    for output in (args.csv, args.plot):
        if output and (output.resolve() == args.capture.resolve() or output.exists()):
            parser.error("output already exists or would overwrite input; choose a new path")
    if args.csv and args.plot and args.csv.resolve() == args.plot.resolve():
        parser.error("CSV and plot must use different paths")
    data = args.capture.read_bytes()
    if metadata.get("sha256") and hashlib.sha256(data).hexdigest() != metadata["sha256"]:
        parser.error("capture SHA-256 differs from its metadata")
    raw, metrics, invalid, skipped, trailing, unsupported, malformed = parse(data, with_stats=True)
    print(f"bytes={len(data)} raw={len(raw)} metrics={len(metrics)} "
          f"checksum_errors={invalid} skipped_bytes={skipped} trailing_bytes={trailing} "
          f"unsupported_packets={unsupported} malformed_packets={malformed}")
    if raw:
        print(f"raw_min={min(raw)} raw_max={max(raw)} raw_nonzero={sum(x != 0 for x in raw)}")
    if metrics:
        print("signal_values=" + ",".join(str(m["signal"]) for m in metrics))
        spacings = [b["raw_index"] - a["raw_index"] for a, b in zip(metrics, metrics[1:])]
        if spacings:
            print(f"raw_per_metric_min={min(spacings)} raw_per_metric_max={max(spacings)}")
    artifact = [False] * len(raw)
    for metric in metrics:
        for i in range(max(0, metric["raw_index"] - 8), min(len(raw), metric["raw_index"] + 30)):
            artifact[i] = True
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(("sample_index", "time_s_approx", "raw", "metric_adjacent_flag"))
            for i, value in enumerate(raw):
                t = i * args.duration / len(raw) if args.duration and raw else ""
                writer.writerow((i, t, value, int(artifact[i])))
        print(f"csv={args.csv}")
    if args.plot:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise SystemExit("matplotlib missing: python -m pip install matplotlib") from exc
        x = [i * args.duration / len(raw) for i in range(len(raw))] if args.duration and raw else range(len(raw))
        fig, ax = plt.subplots(figsize=(15, 5))
        ax.plot(x, raw, linewidth=0.5, color="#23679b")
        ax.set_xlabel("Approximate time (s)" if args.duration else "Sample index")
        ax.set_ylabel("Raw ADC units")
        ax.set_title("EEG module raw output (not diagnostic ECG)")
        ax.grid(alpha=0.2)
        fig.tight_layout()
        args.plot.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(args.plot, dpi=160)
        plt.close(fig)
        print(f"plot={args.plot}")


if __name__ == "__main__":
    main()
