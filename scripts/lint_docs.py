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

# zh-CN relative path -> en relative path (per spec §2.1 + §2.2).
BILINGUAL_MAPPING: dict[str, str] = {
    "00-入门/01-5分钟快速体验.md": "00-getting-started/01-quick-start.md",
    "00-入门/02-文档索引.md": "00-getting-started/02-doc-index.md",
    "00-入门/03-术语速查卡片.md": "00-getting-started/03-glossary.md",
    "00-入门/04-开发者入门指南.md": "00-getting-started/04-developer-guide.md",
    "10-环境与构建/11-环境搭建与配置指南.md": "10-environment-build/11-environment-setup.md",
    "10-环境与构建/12-独立应用构建说明.md": "10-environment-build/12-freestanding-app-build.md",
    "10-环境与构建/13-板型迁移指南.md": "10-environment-build/13-board-porting-guide.md",
    "20-架构设计/21-模块化软件设计方法论.md": "20-architecture/21-modular-design-methodology.md",
    "20-架构设计/22-模块化软件设计的详细方法.md": "20-architecture/22-modular-design-detailed.md",
    "20-架构设计/23-框架核心技术实现细节.md": "20-architecture/23-framework-internals.md",
    "30-核心模块/31-事件系统详细使用说明.md": "30-core-modules/31-event-system-guide.md",
    "30-核心模块/32-模块系统详细使用说明.md": "30-core-modules/32-module-system-guide.md",
    "30-核心模块/33-Thread_IPC服务使用说明.md": "30-core-modules/33-thread-ipc-service-guide.md",
    "30-核心模块/34-Thread_IPC模块集成指南.md": "30-core-modules/34-thread-ipc-integration-guide.md",
    "30-核心模块/35-IPC服务扩展特性规划.md": "30-core-modules/35-ipc-service-roadmap.md",
    "30-核心模块/36-系统服务使用说明.md": "30-core-modules/36-system-services-guide.md",
    "40-应用开发/41-Zephyr应用开发与服务指南.md": "40-app-development/41-zephyr-app-development.md",
    "40-应用开发/42-项目配置项说明.md": "40-app-development/42-config-options.md",
    "40-应用开发/43-配置方案对比指南.md": "40-app-development/43-config-comparison-guide.md",
    "40-应用开发/44-设备树与内存配置手册.md": "40-app-development/44-devicetree-memory-config.md",
    "50-测试与CI/51-单元测试与持续集成说明.md": "50-testing-ci/51-unit-testing-ci.md",
    "50-测试与CI/52-CI平台配置保姆级手册.md": "50-testing-ci/52-ci-platform-setup.md",
    "50-测试与CI/53-硬件测试运行指南.md": "50-testing-ci/53-hardware-testing.md",
    "50-测试与CI/54-watchdog_test_guide.md": "50-testing-ci/54-watchdog-test-guide.md",
    "60-调试与排错/61-烧录与调试快速指南.md": "60-debugging/61-flash-debug-quickstart.md",
    "60-调试与排错/62-常见问题与故障排除.md": "60-debugging/62-troubleshooting.md",
    "60-调试与排错/63-脚本与工具说明.md": "60-debugging/63-scripts-and-tools.md",
    "70-发布与产品化/71-版本管理.md": "70-release-productization/71-version-management.md",
    "70-发布与产品化/72-Zephyr版本与CI说明.md": "70-release-productization/72-zephyr-version-ci.md",
    "70-发布与产品化/73-发布检查清单.md": "70-release-productization/73-release-checklist.md",
    "70-发布与产品化/74-OTA与存储扩展指南.md": "70-release-productization/74-ota-storage-guide.md",
    "70-发布与产品化/75-安全与密钥管理说明.md": "70-release-productization/75-security-key-management.md",
    "80-贡献与维护/81-参与贡献与代码规范.md": "80-contributing/81-contributing-code-style.md",
    "80-贡献与维护/82-文档改进建议.md": "80-contributing/82-doc-improvements.md",
}


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


def check_bilingual_parity(
    zh_root: Path, en_root: Path, mapping: dict[str, str]
) -> list[Issue]:
    """For each mapping entry, both sides must exist."""
    issues: list[Issue] = []
    for zh_rel, en_rel in mapping.items():
        zh = zh_root / zh_rel
        en = en_root / en_rel
        if zh.exists() and not en.exists():
            issues.append(
                Issue(
                    file=zh,
                    line=0,
                    kind="missing-en-counterpart",
                    message=f"zh has no English counterpart: expected {en}",
                )
            )
        elif en.exists() and not zh.exists():
            issues.append(
                Issue(
                    file=en,
                    line=0,
                    kind="missing-zh-counterpart",
                    message=f"en has no Chinese counterpart: expected {zh}",
                )
            )
    return issues


_CJK_RE = re.compile(r"[一-鿿]+")


def check_residual_chinese(roots: list[Path]) -> list[Issue]:
    """Flag CJK chars outside of fenced code blocks in English-tagged docs."""
    issues: list[Issue] = []
    for md in _iter_md_files(roots):
        try:
            text = md.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        in_code = False
        for lineno, line in enumerate(text.splitlines(), start=1):
            stripped = line.lstrip()
            if stripped.startswith("```"):
                in_code = not in_code
                continue
            if in_code:
                continue
            if line.lstrip().startswith("> Language:") or line.lstrip().startswith("> 语言:"):
                continue
            m = _CJK_RE.search(line)
            if m:
                issues.append(
                    Issue(
                        file=md,
                        line=lineno,
                        kind="residual-chinese",
                        message=f"Chinese chars in EN doc: {m.group(0)[:20]}",
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
    parser.add_argument(
        "--check-parity",
        action="store_true",
        help="Run bilingual parity check (zh-CN <-> en)",
    )
    parser.add_argument(
        "--check-residual-chinese",
        action="store_true",
        help="Flag Chinese chars in docs/en/ files (outside code blocks)",
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
    if args.check_parity:
        issues += check_bilingual_parity(
            Path("docs/zh-CN"), Path("docs/en"),
            mapping=BILINGUAL_MAPPING,
        )
    if args.check_residual_chinese:
        issues += check_residual_chinese([Path("docs/en")])
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
