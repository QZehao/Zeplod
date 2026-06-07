#!/usr/bin/env python3
"""Validate that script names referenced in docs exist under scripts/."""

from __future__ import annotations

import re
import sys
from pathlib import Path


DOC_FILES = [
    Path("docs/zh-CN/60-调试与排错/63-脚本与工具说明.md"),
    Path("docs/en/60-debugging/63-scripts-and-tools.md"),
    Path("docs/zh-CN/10-环境与构建/11-环境搭建与配置指南.md"),
    Path("docs/en/10-environment-build/11-environment-setup.md"),
    Path("docs/zh-CN/10-环境与构建/14-QEMU仿真运行指南.md"),
    Path("docs/en/10-environment-build/14-qemu-simulation-guide.md"),
    Path("docs/zh-CN/10-环境与构建/15-新建APP开发指南.md"),
    Path("docs/en/10-environment-build/15-creating-new-app-guide.md"),
]

PATTERN = re.compile(r"`([A-Za-z0-9_.-]+\.(?:ps1|sh|bat|py))`")


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from project_layout import resolve_project_layout

    layout = resolve_project_layout()
    repo = layout.framework_root
    scripts_dir = layout.scripts_root
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
