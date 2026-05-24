#!/usr/bin/env python3
"""Validate that script names referenced in docs exist under scripts/."""

from __future__ import annotations

import re
import sys
from pathlib import Path


DOC_FILES = [
    Path("docs/zh-CN/60-调试与排错/63-脚本与工具说明.md"),
    Path("docs/en/60-debugging/63-scripts-and-tools.md"),
]

PATTERN = re.compile(r"`([A-Za-z0-9_.-]+\.(?:ps1|sh|bat|py))`")


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    scripts_dir = repo / "scripts"
    existing = {p.name for p in scripts_dir.iterdir() if p.is_file()}
    missing: list[str] = []

    for doc in DOC_FILES:
        text = (repo / doc).read_text(encoding="utf-8")
        for name in PATTERN.findall(text):
            if name not in existing:
                missing.append(f"{doc}: {name}")

    if missing:
        print("Script docs reference missing files:")
        for item in sorted(set(missing)):
            print(f"  - {item}")
        return 1

    print("Script docs check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
