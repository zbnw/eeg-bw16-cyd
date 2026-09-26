"""Build fresh firmware folders, reject vendor false successes, and hash artifacts.

No upload is performed. Arduino SDK versions must already be installed.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "cyd": ("esp32:esp32:jczn_2432s028r", "esp32_cyd_starter", "esp32_cyd_starter.ino.bin"),
    "bw16": ("realtek:AmebaD:Ai-Thinker_BW16", "bw16_ble_eeg", "km0_km4_image2.bin"),
    "reader": ("realtek:AmebaD:Ai-Thinker_BW16", "bw16_eeg_reader", "km0_km4_image2.bin"),
}


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=(*TARGETS, "all"), default="all")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cli", type=Path, help="Arduino CLI executable; defaults to local .tools copy or PATH")
    args = parser.parse_args()
    output = args.output or ROOT / ".build" / ("release-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    local_cli = ROOT / ".tools/arduino-cli/arduino-cli.exe"
    cli = args.cli or (local_cli if local_cli.is_file() else shutil.which("arduino-cli"))
    if not cli:
        parser.error("Arduino CLI not found; install it, add it to PATH, or pass --cli")
    report = {"firmware_version": "0.2.0", "built_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "flashed": False, "targets": {}, "source_sha256": {}}
    for directory in ("sketches", "libraries"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.is_file():
                report["source_sha256"][path.relative_to(ROOT).as_posix()] = sha(path)
    for field, command in (("arduino_cli", ["version"]), ("installed_cores", ["core", "list"])):
        report[field] = subprocess.check_output([str(cli), *command], text=True, encoding="utf-8", errors="replace")
    failed = False
    for target in TARGETS if args.target == "all" else (args.target,):
        fqbn, sketch, binary = TARGETS[target]
        build = output / target
        command = [str(cli), "compile", "--fqbn", fqbn, "--libraries", str(ROOT / "libraries"),
                   "--build-path", str(build), str(ROOT / "sketches" / sketch)]
        print(f"Building {target}...", flush=True)
        result = subprocess.run(command, cwd=ROOT, text=True, encoding="utf-8", errors="replace", capture_output=True)
        log = result.stdout + result.stderr
        (output / f"{target}.log").write_text(log, encoding="utf-8")
        artifact = build / binary
        bad_log = any(token in log.lower() for token in ("access denied", "access is denied", "permission denied", "error:", "拒绝访问"))
        valid = result.returncode == 0 and not bad_log and artifact.is_file() and artifact.stat().st_size > 0
        entry = {"fqbn": fqbn, "command": command, "verified_build": valid, "artifacts": {}}
        if valid:
            for path in sorted(build.glob("*.bin")):
                entry["artifacts"][path.name] = {"bytes": path.stat().st_size, "sha256": sha(path)}
        else:
            failed = True
        report["targets"][target] = entry
        (output / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(log[-1800:], flush=True)
        print(f"{target}: {'PASS' if valid else 'FAIL'}", flush=True)
    changed = [name for name, digest in report["source_sha256"].items() if sha(ROOT / name) != digest]
    report["source_changed_during_build"] = changed
    if changed:
        failed = True
        print("FAIL: sources changed during build; rebuild a fresh release")
    report["verified_release"] = not failed
    (output / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Manifest: {output / 'manifest.json'}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
