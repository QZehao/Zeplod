#!/usr/bin/env python3
"""Inventory docs/ tree: list markdown files, index coverage, bilingual pairing.

Usage:
    python scripts/doc_inventory.py
    python scripts/doc_inventory.py --json report.json
    python scripts/doc_inventory.py --strict   # exit 1 if unindexed zh-CN or missing en pair
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

MD_LINK_RE = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:", "ftp://")

INDEX_FILES = [
    Path("docs/zh-CN/00-入门/02-文档索引.md"),
    Path("docs/en/00-getting-started/02-doc-index.md"),
]


def _load_mapping() -> dict[str, str]:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from lint_docs import BILINGUAL_MAPPING

    return BILINGUAL_MAPPING


def _indexed_paths(repo: Path) -> set[str]:
    """Collect relative paths (from docs/zh-CN/) linked from the Chinese index."""
    indexed: set[str] = set()
    index = repo / INDEX_FILES[0]
    if not index.exists():
        return indexed
    text = index.read_text(encoding="utf-8")
    base = index.parent
    for _label, target in MD_LINK_RE.findall(text):
        target = target.split()[0].split("#", 1)[0]
        if not target or target.startswith(EXTERNAL_PREFIXES):
            continue
        if not target.endswith(".md"):
            continue
        resolved = (base / target).resolve()
        try:
            rel = resolved.relative_to((repo / "docs/zh-CN").resolve())
            indexed.add(str(rel).replace("\\", "/"))
        except ValueError:
            pass
    return indexed


def _list_zh_md(repo: Path) -> list[Path]:
    zh_root = repo / "docs/zh-CN"
    if not zh_root.is_dir():
        return []
    return sorted(zh_root.rglob("*.md"))


def build_report(repo: Path) -> dict:
    mapping = _load_mapping()
    indexed = _indexed_paths(repo)
    zh_root = repo / "docs/zh-CN"
    en_root = repo / "docs/en"

    entries = []
    for md in _list_zh_md(repo):
        rel = md.relative_to(zh_root).as_posix()
        en_rel = mapping.get(rel)
        en_exists = bool(en_rel and (en_root / en_rel).exists())
        entries.append(
            {
                "zh": rel,
                "in_index": rel in indexed,
                "en_mapped": en_rel,
                "en_exists": en_exists,
                "paired": en_exists if en_rel else None,
            }
        )

    unindexed = [e["zh"] for e in entries if not e["in_index"] and not e["zh"].endswith("02-文档索引.md")]
    missing_en = [e["zh"] for e in entries if e["en_mapped"] and not e["en_exists"]]
    unmapped = [e["zh"] for e in entries if e["en_mapped"] is None]

    return {
        "total_zh": len(entries),
        "indexed_count": sum(1 for e in entries if e["in_index"]),
        "unindexed": unindexed,
        "missing_en": missing_en,
        "unmapped_zh": unmapped,
        "entries": entries,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Docs inventory report.")
    parser.add_argument("--json", type=Path, help="Write JSON report to path")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit 1 if mapped zh doc lacks en counterpart",
    )
    args = parser.parse_args(argv)

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from project_layout import resolve_project_layout

    repo = resolve_project_layout().framework_root
    report = build_report(repo)

    print(f"zh-CN markdown files: {report['total_zh']}")
    print(f"linked from 02-文档索引: {report['indexed_count']}")
    if report["unindexed"]:
        print(f"not in index ({len(report['unindexed'])}):")
        for p in report["unindexed"]:
            print(f"  - {p}")
    if report["missing_en"]:
        print(f"missing en counterpart ({len(report['missing_en'])}):")
        for p in report["missing_en"]:
            print(f"  - {p}")
    if report["unmapped_zh"]:
        print(f"no BILINGUAL_MAPPING entry ({len(report['unmapped_zh'])}):")
        for p in report["unmapped_zh"]:
            print(f"  - {p}")

    if args.json:
        args.json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Wrote {args.json}")

    if args.strict and report["missing_en"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
