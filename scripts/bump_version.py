#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0
"""同步语义化版本到 APP_VERSION、Doxyfile、README（CMake 从 APP_VERSION 读取）。

勿使用根目录文件名 VERSION：会与 Zephyr find_package 的内核版本解析冲突。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from project_layout import resolve_project_layout

_LAYOUT = resolve_project_layout()
ROOT = _LAYOUT.framework_root
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def read_version(path: Path) -> str:
    text = path.read_text(encoding="utf-8").strip().splitlines()
    if not text:
        sys.exit("APP_VERSION is empty")
    v = text[0].strip()
    if not VER_RE.match(v):
        sys.exit(f"Invalid version (expected X.Y.Z): {v!r}")
    return v


def write_version(path: Path, version: str) -> None:
    path.write_text(version + "\n", encoding="utf-8")


def patch_doxyfile(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    text, n = re.subn(
        r'^PROJECT_NUMBER\s*=\s*".*"',
        f'PROJECT_NUMBER         = "{version}"',
        text,
        count=1,
        flags=re.MULTILINE,
    )
    if n != 1:
        sys.exit("Doxyfile: PROJECT_NUMBER not found or not unique")
    path.write_text(text, encoding="utf-8")


def patch_readme(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    text, n = re.subn(
        # README.md 中“版本”行通常形如：
        # **版本**：1.0.0（…其它说明…）
        # 这里只替换其中的 X.Y.Z，不要求整行只包含版本号。
        r"^(\*\*版本\*\*：)\s*([\d]+\.[\d]+\.[\d]+)(.*)$",
        rf"\g<1>{version}\g<3>",
        text,
        count=1,
        flags=re.MULTILINE,
    )
    if n != 1:
        sys.exit("README.md: **版本**： line not found")
    path.write_text(text, encoding="utf-8")


def main() -> None:
    p = argparse.ArgumentParser(description="Bump project version (X.Y.Z)")
    p.add_argument(
        "version",
        nargs="?",
        help="New version, e.g. 1.0.1 (omit to print current APP_VERSION only)",
    )
    args = p.parse_args()

    vfile = _LAYOUT.app_version_file()
    if vfile is None:
        sys.exit("APP_VERSION not found under app or framework root")
    current = read_version(vfile)
    if args.version is None:
        print(current)
        return

    if not VER_RE.match(args.version):
        sys.exit("version must be X.Y.Z")

    write_version(vfile, args.version)
    patch_doxyfile(ROOT / "Doxyfile", args.version)
    readme = _LAYOUT.app_root / "README.md"
    if not readme.is_file():
        readme = ROOT / "README.md"
    patch_readme(readme, args.version)
    print(f"Version set to {args.version} (was {current})")
    print("Next: git diff, commit, and re-run CMake (e.g. west build -t pristine).")


if __name__ == "__main__":
    main()
