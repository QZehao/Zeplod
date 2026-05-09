#!/usr/bin/env python3
"""Lint script for docs/ tree.

Checks:
1. Markdown links in *.md files - target file must exist.
2. CONFIG_* references - macro must exist in Kconfig*.

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


def check_md_links(roots: list[Path]) -> list[Issue]:
    """Find broken markdown links pointing at local files."""
    issues: list[Issue] = []
    for md in _iter_md_files(roots):
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


CONFIG_RE = re.compile(r"\bCONFIG_[A-Z0-9_]+")
KCONFIG_DECL_RE = re.compile(r"^\s*(?:config|menuconfig)\s+([A-Z0-9_]+)")


def _collect_kconfig_symbols(kconfig_files: list[Path]) -> set[str]:
    symbols: set[str] = set()
    for kfile in kconfig_files:
        if not kfile.exists():
            continue
        try:
            for line in kfile.read_text(encoding="utf-8").splitlines():
                m = KCONFIG_DECL_RE.match(line)
                if m:
                    symbols.add(f"CONFIG_{m.group(1)}")
        except UnicodeDecodeError:
            continue
    return symbols


def check_config_macros(
    roots: list[Path], kconfig_files: list[Path]
) -> list[Issue]:
    """Flag CONFIG_* macros referenced in *.md but not defined in any Kconfig."""
    defined = _collect_kconfig_symbols(kconfig_files)
    issues: list[Issue] = []
    for md in _iter_md_files(roots):
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
    args = parser.parse_args(argv)
    roots = [Path(p) for p in args.roots]

    issues = check_md_links(roots)
    if not args.skip_config_check:
        kconfig_files = [Path(p) for p in (args.kconfig or [
            "Kconfig", "Kconfig_proprietary"
        ])]
        issues += check_config_macros(roots, kconfig_files)
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
