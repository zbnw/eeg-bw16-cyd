"""Verify every file listed in a complete EEG backup's SHA256SUMS.csv."""

import argparse
import csv
import hashlib
from pathlib import Path


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backup", type=Path, help="Backup directory containing SHA256SUMS.csv")
    args = parser.parse_args()
    root = args.backup.resolve()
    manifest = root / "SHA256SUMS.csv"
    failed = 0
    count = 0
    listed = set()
    with manifest.open("r", newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            if row["path"] in listed:
                print(f"DUPLICATE: {row['path']}")
                failed += 1
            listed.add(row["path"])
            relative = Path(row["path"])
            target = (root / relative).resolve()
            count += 1
            if not target.is_relative_to(root) or not target.is_file():
                print(f"MISSING: {relative}")
                failed += 1
            elif target.stat().st_size != int(row["size_bytes"]) or digest(target) != row["sha256"]:
                print(f"MISMATCH: {relative}")
                failed += 1
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file() and path != manifest}
    for extra in sorted(actual - listed):
        print(f"EXTRA: {extra}")
        failed += 1
    print(f"Checked {count} listed files; problems={failed}")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
