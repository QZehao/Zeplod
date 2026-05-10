#!/usr/bin/env python3
"""Lint script for docs/ tree.

Checks:
1. Markdown links in *.md files - target file must exist.
2. CONFIG_* references — macro must exist in `Kconfig` or `Kconfig_proprietary`
   (or files passed via `--kconfig`); `source`/`rsource`/`osource` directives are
   followed transitively.

Usage:
    python scripts/lint_docs.py
    python scripts/lint_docs.py --strict       # exit 1 on any issue
    python scripts/lint_docs.py --report PATH  # write Markdown report
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

MD_LINK_RE = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:", "ftp://")


@dataclass
class Issue:
    """A single lint finding."""

    file: Path
    line: int
    kind: str
    message: str


def _iter_md_files(roots: list[Path]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        if root.is_file() and root.suffix == ".md":
            files.append(root)
        elif root.is_dir():
            files.extend(sorted(root.rglob("*.md")))
    return files


def check_md_links(
    roots: list[Path], excludes: list[Path] | None = None
) -> list[Issue]:
    """Find broken markdown links pointing at local files."""
    excludes = excludes or []
    issues: list[Issue] = []
    for md in _iter_md_files(roots):
        if _is_excluded(md, excludes):
            continue
        try:
            text = md.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            for _label, target in MD_LINK_RE.findall(line):
                target = target.split()[0]  # strip "title" suffix
                target = target.split("#", 1)[0]  # strip anchor
                if not target or target.startswith(EXTERNAL_PREFIXES):
                    continue
                resolved = (md.parent / target).resolve()
                if not resolved.exists():
                    issues.append(
                        Issue(
                            file=md,
                            line=lineno,
                            kind="broken-link",
                            message=f"link target not found: {target}",
                        )
                    )
    return issues


def _is_excluded(path: Path, excludes: list[Path]) -> bool:
    if not excludes:
        return False
    try:
        resolved = path.resolve()
    except OSError:
        return False
    for ex in excludes:
        try:
            resolved.relative_to(ex.resolve())
            return True
        except (ValueError, OSError):
            continue
    return False


CONFIG_RE = re.compile(r"\bCONFIG_[A-Z0-9_]+")
KCONFIG_DECL_RE = re.compile(r"^\s*(?:config|menuconfig)\s+([A-Z0-9_]+)")
SOURCE_RE = re.compile(r'^\s*([or]?source)\s+"([^"]+)"')


def _collect_kconfig_symbols(kconfig_files: list[Path]) -> set[str]:
    symbols: set[str] = set()
    seen: set[Path] = set()
    queue: list[Path] = [Path(p) for p in kconfig_files]
    while queue:
        kfile = queue.pop()
        try:
            kfile = kfile.resolve()
        except OSError:
            continue
        if kfile in seen or not kfile.exists():
            continue
        seen.add(kfile)
        try:
            text = kfile.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for line in text.splitlines():
            m = KCONFIG_DECL_RE.match(line)
            if m:
                symbols.add(f"CONFIG_{m.group(1)}")
                continue
            sm = SOURCE_RE.match(line)
            if sm and "$" not in sm.group(2):
                queue.extend(kfile.parent.glob(sm.group(2)))
    return symbols


def check_config_macros(
    roots: list[Path],
    kconfig_files: list[Path],
    excludes: list[Path] | None = None,
) -> list[Issue]:
    """Flag CONFIG_* macros referenced in *.md but not defined in any Kconfig."""
    excludes = excludes or []
    defined = _collect_kconfig_symbols(kconfig_files)
    issues: list[Issue] = []
    for md in _iter_md_files(roots):
        if _is_excluded(md, excludes):
            continue
        try:
            text = md.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            for sym in CONFIG_RE.findall(line):
                if sym not in defined:
                    issues.append(
                        Issue(
                            file=md,
                            line=lineno,
                            kind="missing-config",
                            message=f"undefined macro: {sym}",
                        )
                    )
    return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Lint docs tree.")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "roots",
        nargs="*",
        default=["docs"],
        help="Root paths to scan (default: docs)",
    )
    parser.add_argument(
        "--kconfig",
        action="append",
        default=None,
        help="Kconfig file(s) to consult (default: Kconfig + Kconfig_proprietary)",
    )
    parser.add_argument(
        "--skip-config-check",
        action="store_true",
        help="Skip CONFIG_* macro existence check",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=None,
        help="Path prefix to skip (repeatable; e.g. --exclude docs/superpowers)",
    )
    args = parser.parse_args(argv)
    roots = [Path(p) for p in args.roots]

    excludes = [Path(p) for p in (args.exclude or [])]
    issues = check_md_links(roots, excludes=excludes)
    if not args.skip_config_check:
        kconfig_files = [Path(p) for p in (args.kconfig or [
            "Kconfig", "Kconfig_proprietary"
        ])]
        issues += check_config_macros(roots, kconfig_files, excludes=excludes)
    for issue in issues:
        print(f"{issue.file}:{issue.line}: [{issue.kind}] {issue.message}")
    if args.report:
        lines = ["# Docs Lint Report", ""]
        for issue in issues:
            lines.append(
                f"- `{issue.file}:{issue.line}` **{issue.kind}**: {issue.message}"
            )
        if not issues:
            lines.append("- (no issues found)")
        args.report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if issues and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
