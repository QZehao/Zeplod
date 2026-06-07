#!/usr/bin/env python3
"""Check text files are UTF-8 decodable and do not contain UTF-8 BOM."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


TEXT_SUFFIXES = {
    ".c",
    ".h",
    ".cpp",
    ".hpp",
    ".py",
    ".sh",
    ".ps1",
    ".bat",
    ".cmake",
    ".txt",
    ".md",
    ".yaml",
    ".yml",
    ".conf",
    ".overlay",
    ".dts",
    ".kconfig",
}

TEXT_FILENAMES = {
    "CMakeLists.txt",
    "Kconfig",
    "Kconfig.zephyr",
    "Kconfig_proprietary",
    "APP_VERSION",
    "Doxyfile",
    ".gitignore",
    ".editorconfig",
}

EXCLUDE_PREFIXES = (
    ".git/",
    "build/",
    "build_",
    "release/",
    "Testing/",
    ".cache/",
    ".vscode/",
)

KNOWN_BOM_ALLOWLIST = {
    "scripts/analyze_map.ps1",
    "scripts/generate_docs.ps1",
    "scripts/package_release.ps1",
}


def is_text_path(path: Path) -> bool:
    name = path.name
    if name in TEXT_FILENAMES:
        return True
    if path.suffix.lower() in TEXT_SUFFIXES:
        return True
    return False


def is_excluded(rel: str) -> bool:
    rel_norm = rel.replace("\\", "/")
    return any(rel_norm.startswith(prefix) for prefix in EXCLUDE_PREFIXES)


def git_tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files"],
        capture_output=True,
        check=True,
    )
    stdout = result.stdout.decode("utf-8", errors="replace")
    return [line.strip() for line in stdout.splitlines() if line.strip()]


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from project_layout import resolve_project_layout

    layout = resolve_project_layout()
    repo = layout.work_root
    bad_decode: list[str] = []
    bad_bom: list[str] = []

    for rel in git_tracked_files():
        if is_excluded(rel):
            continue
        path = repo / rel
        if not path.is_file() or not is_text_path(path):
            continue

        data = path.read_bytes()
        if data.startswith(b"\xef\xbb\xbf") and rel not in KNOWN_BOM_ALLOWLIST:
            bad_bom.append(rel)
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            bad_decode.append(rel)

    if bad_decode or bad_bom:
        if bad_decode:
            print("Non-UTF-8 text files:")
            for p in bad_decode:
                print(f"  - {p}")
        if bad_bom:
            print("UTF-8 BOM detected (use UTF-8 without BOM):")
            for p in bad_bom:
                print(f"  - {p}")
        return 1

    print("Encoding check passed (UTF-8, no BOM).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
