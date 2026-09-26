"""Make and verify a dated full local backup of this project plus H: EEG assets.

Usage: python tools/create_backup.py --destination PATH_OUTSIDE_PROJECT
The script never deletes or overwrites existing backup folders or archives.
"""

import argparse
import csv
import datetime
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


PROJECT = Path(__file__).resolve().parents[1]
EXTERNAL_NAMES = (
    "软件开发包", "TGAM脑波开发SDK", "调试软件.exe", "configure.dat",
    "Realterm.lnk",
    "sscom5.13.1.exe", "sscom51.ini",
)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--name", default="Esp32-EEG-Complete-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--project-only", action="store_true", help="Do not require the historical H: external assets")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if not args.name or Path(args.name).name != args.name or args.name in (".", ".."):
        parser.error("--name must be a single directory name")
    if destination.is_relative_to(PROJECT):
        parser.error("destination must be outside the project (avoids recursively copying the backup into itself)")
    root = destination / args.name
    archive = destination / (args.name + ".zip")
    if root.exists() or archive.exists():
        parser.error("backup folder or ZIP already exists; choose another --name")
    if not destination.is_dir():
        parser.error("destination directory does not exist")
    external_names = () if args.project_only else EXTERNAL_NAMES
    for name in external_names:
        if not (destination / name).exists():
            parser.error(f"external source missing: {name}")

    root.mkdir()
    print("Copying entire project...", flush=True)
    shutil.copytree(PROJECT, root / "project", ignore=shutil.ignore_patterns(".venv", "__pycache__"))
    external = root / "external_software"
    external.mkdir()
    for name in external_names:
        source = destination / name
        target = external / name
        if source.is_dir():
            shutil.copytree(source, target)
        else:
            shutil.copy2(source, target)
    (root / "README_BACKUP.md").write_text(
        "# EEG 完整本地备份\n\n"
        + ("本次仅备份项目，不包含第三方 H 盘资料。\n\n" if args.project_only else "本次包含指定的 H 盘第三方资料。\n\n") +
        "请先阅读 [项目导览](project/docs/00_项目导览.md)、"
        "[接线](project/docs/01_接线与上电.md) 与"
        "[安全说明](project/hardware/SAFETY.md)。\n\n"
        "`project/` 是工程目录的完整快照，含源码、文档、聊天记录、"
        "历史 BIN/CSV/PNG、编译产物和本地 Arduino CLI；排除可重建的 .venv 与 __pycache__。"
        "`external_software/` 是备份前 H:\\Desktop\\EEG 下用户已有的第三方工具/SDK快照；"
        "没有修改原文件，也没有核验第三方许可。\n\n"
        "`SHA256SUMS.csv` 列出所有文件的相对路径、长度与 SHA-256。"
        "在此目录运行：\n\n"
        "```powershell\npython .\\project\\tools\\verify_backup.py .\n```\n\n"
        "恢复后仍需安装 Arduino 平台包、USB 驱动并确认 COM 号。"
        "备份内的二进制固件文件不代表设备当前已刷入该版本。"
        "本项目是非医疗原型。\n",
        encoding="utf-8",
    )

    files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix())
    manifest = root / "SHA256SUMS.csv"
    print(f"Hashing {len(files)} backup files...", flush=True)
    with manifest.open("w", newline="", encoding="utf-8-sig") as output:
        writer = csv.writer(output)
        writer.writerow(("path", "size_bytes", "sha256"))
        for file in files:
            writer.writerow((file.relative_to(root).as_posix(), file.stat().st_size, digest(file)))
    print("Verifying manifest...", flush=True)
    subprocess.run([sys.executable, str(root / "project/tools/verify_backup.py"), str(root)], check=True)

    print("Creating ZIP archive...", flush=True)
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as output:
        for file in sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix()):
            output.write(file, (Path(args.name) / file.relative_to(root)).as_posix())
    with zipfile.ZipFile(archive) as source:
        bad = source.testzip()
        if bad:
            raise RuntimeError(f"ZIP CRC error: {bad}")
        zip_files = len(source.namelist())
    total = sum(file.stat().st_size for file in root.rglob("*") if file.is_file())
    print(f"BACKUP={root}")
    print(f"FILES={len(files)} BYTES={total}")
    print(f"ZIP={archive} ZIP_BYTES={archive.stat().st_size} ZIP_ENTRIES={zip_files}")
    print(f"ZIP_SHA256={digest(archive)}")


if __name__ == "__main__":
    main()
