"""Build Chinese GitHub Wiki pages from repository documentation."""

import argparse
import pathlib
import posixpath
import re
from urllib.parse import quote


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "wiki"
REPO = "https://github.com/zbnw/eeg-bw16-cyd"
PAGES = {
    "README.md": "Home",
    "docs/00_项目导览.md": "项目导览",
    "docs/01_接线与上电.md": "接线与上电",
    "docs/02_安装与使用.md": "安装与使用",
    "docs/03_协议与核心逻辑.md": "协议与核心逻辑",
    "docs/04_采集与数据处理.md": "采集与数据处理",
    "docs/05_开发维护.md": "开发维护",
    "docs/06_测试记录.md": "测试记录",
    "docs/07_故障排查.md": "故障排查",
    "docs/08_备份与复原.md": "备份与复原",
    "docs/09_v0.2升级与验收.md": "0.2升级与验收",
    "docs/GITHUB发布检查.md": "发布与资料边界",
    "EEG_WIRING.md": "EEG接线详解",
    "BLE_LINK.md": "BLE通信协议",
    "hardware/SICHIRAY_EEG_V6_1.md": "EEG模块集成",
    "hardware/BW16_EEG_BASEBOARD.md": "BW16底板连接规范",
    "hardware/SAFETY.md": "原型安全边界",
    "web/README.md": "网页与网关使用",
    "web/PROTOCOL.md": "网页与网关协议",
    "sketches/bw16_eeg_reader/README.md": "BW16串口诊断",
}
LINK = re.compile(r"(?<!!)\[([^\]]+)\]\(([^)]+)\)")


def wiki_url(title):
    return REPO + "/wiki/" + quote(title)


def rewrite_links(source, text):
    def replace(match):
        label, target = match.groups()
        if target.startswith(("https://", "http://", "mailto:", "#")):
            return match.group(0)
        filename, marker, fragment = target.partition("#")
        normalized = posixpath.normpath(posixpath.join(posixpath.dirname(source), filename))
        if normalized in PAGES:
            url = wiki_url(PAGES[normalized])
        else:
            url = REPO + "/blob/main/" + quote(normalized, safe="/")
        if marker:
            url += "#" + quote(fragment, safe="-")
        return f"[{label}]({url})"

    return LINK.sub(replace, text)


def build_pages():
    pages = {}
    for source, title in PAGES.items():
        content = (ROOT / source).read_text(encoding="utf-8")
        if not content.endswith("\n"):
            content += "\n"
        content = rewrite_links(source, content)
        source_url = REPO + "/blob/main/" + quote(source, safe="/")
        pages[title + ".md"] = content + f"\n---\n\n本页与[仓库原文]({source_url})同步。\n"

    groups = [
        ("入门", ["Home", "项目导览", "接线与上电", "安装与使用", "0.2升级与验收"]),
        ("数据与排障", ["采集与数据处理", "网页与网关使用", "故障排查", "测试记录"]),
        ("协议与开发", ["协议与核心逻辑", "BLE通信协议", "网页与网关协议", "开发维护", "BW16串口诊断"]),
        ("硬件与维护", ["EEG接线详解", "EEG模块集成", "BW16底板连接规范", "原型安全边界", "备份与复原", "发布与资料边界"]),
    ]
    lines = ["# 中文文档导航", ""]
    for group, titles in groups:
        lines.extend([f"## {group}", ""])
        lines.extend(f"- [{'首页' if title == 'Home' else title}]({wiki_url(title)})" for title in titles)
        lines.append("")
    pages["_Sidebar.md"] = "\n".join(lines)
    return pages


def main():
    parser = argparse.ArgumentParser(description="同步项目文档到可发布的中文 Wiki 源文件")
    parser.add_argument("--check", action="store_true", help="只检查 wiki/ 是否与原文一致")
    args = parser.parse_args()
    pages = build_pages()
    if args.check:
        stale = [name for name, content in pages.items() if not (OUTPUT / name).is_file() or (OUTPUT / name).read_text(encoding="utf-8") != content]
        if stale:
            parser.exit(1, "Wiki 页面需要更新：" + "、".join(stale) + "\n")
    else:
        OUTPUT.mkdir(exist_ok=True)
        for name, content in pages.items():
            (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print(f"已检查 {len(pages)} 个 Wiki 页面")


if __name__ == "__main__":
    main()
