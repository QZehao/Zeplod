"""Smoke tests for scripts/lint_docs.py."""
import sys
from pathlib import Path

# Ensure scripts/ is importable
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import lint_docs  # noqa: E402


def test_finds_broken_link(tmp_path: Path) -> None:
    """Lint must report a broken markdown link."""
    src = tmp_path / "a.md"
    src.write_text("See [missing](missing.md).\n", encoding="utf-8")
    issues = lint_docs.check_md_links([tmp_path])
    assert any("missing.md" in i.message for i in issues)


def test_passes_when_link_target_exists(tmp_path: Path) -> None:
    """Lint must not report when link target exists."""
    target = tmp_path / "b.md"
    target.write_text("# B\n", encoding="utf-8")
    src = tmp_path / "a.md"
    src.write_text("See [b](b.md).\n", encoding="utf-8")
    issues = lint_docs.check_md_links([tmp_path])
    assert not any("b.md" in i.message for i in issues)


def test_ignores_external_urls(tmp_path: Path) -> None:
    """Lint must skip http/https/mailto: links."""
    src = tmp_path / "a.md"
    src.write_text(
        "See [home](https://example.com) or [me](mailto:x@y.z).\n",
        encoding="utf-8",
    )
    issues = lint_docs.check_md_links([tmp_path])
    assert not issues


def test_finds_missing_config(tmp_path: Path) -> None:
    """Lint must flag a CONFIG_* not present in any Kconfig file."""
    kconfig = tmp_path / "Kconfig"
    kconfig.write_text("config FOO\n\tbool\n", encoding="utf-8")
    md = tmp_path / "doc.md"
    md.write_text("Use `CONFIG_BAR` to enable.\n", encoding="utf-8")
    issues = lint_docs.check_config_macros([tmp_path], [kconfig])
    assert any("CONFIG_BAR" in i.message for i in issues)


def test_passes_when_config_exists(tmp_path: Path) -> None:
    """Lint must not flag CONFIG_* present in Kconfig."""
    kconfig = tmp_path / "Kconfig"
    kconfig.write_text("config FOO\n\tbool\n", encoding="utf-8")
    md = tmp_path / "doc.md"
    md.write_text("Use `CONFIG_FOO` to enable.\n", encoding="utf-8")
    issues = lint_docs.check_config_macros([tmp_path], [kconfig])
    assert not any("CONFIG_FOO" in i.message for i in issues)


def test_kconfig_recursion_follows_rsource(tmp_path: Path) -> None:
    """_collect_kconfig_symbols must follow rsource directives."""
    sub = tmp_path / "sub"
    sub.mkdir()
    (sub / "Kconfig").write_text("config DEEP\n\tbool\n", encoding="utf-8")
    root_k = tmp_path / "Kconfig"
    root_k.write_text(
        "config TOP\n\tbool\nrsource \"sub/Kconfig\"\n", encoding="utf-8"
    )
    md = tmp_path / "doc.md"
    md.write_text("Use `CONFIG_DEEP`.\n", encoding="utf-8")
    issues = lint_docs.check_config_macros([tmp_path], [root_k])
    assert not any("CONFIG_DEEP" in i.message for i in issues)


def test_exclude_skips_directory(tmp_path: Path) -> None:
    """--exclude must filter out matching directories from md scan."""
    keep = tmp_path / "keep"
    keep.mkdir()
    (keep / "a.md").write_text("[x](missing.md)\n", encoding="utf-8")
    skip = tmp_path / "skip"
    skip.mkdir()
    (skip / "b.md").write_text("[y](missing.md)\n", encoding="utf-8")
    issues = lint_docs.check_md_links([tmp_path], excludes=[skip])
    assert any("a.md" in str(i.file) for i in issues)
    assert not any("b.md" in str(i.file) for i in issues)


def test_finds_missing_en_counterpart(tmp_path: Path) -> None:
    """zh-CN file with no matching en/ file should be flagged."""
    zh_dir = tmp_path / "zh-CN" / "00-入门"
    en_dir = tmp_path / "en" / "00-getting-started"
    zh_dir.mkdir(parents=True)
    en_dir.mkdir(parents=True)
    (zh_dir / "01-5分钟快速体验.md").write_text("# zh\n", encoding="utf-8")
    issues = lint_docs.check_bilingual_parity(
        tmp_path / "zh-CN", tmp_path / "en",
        mapping={"00-入门/01-5分钟快速体验.md": "00-getting-started/01-quick-start.md"},
    )
    assert any("01-quick-start.md" in i.message for i in issues)


def test_passes_when_en_counterpart_exists(tmp_path: Path) -> None:
    """No issue if both files exist for a mapping entry."""
    zh_dir = tmp_path / "zh-CN" / "00-入门"
    en_dir = tmp_path / "en" / "00-getting-started"
    zh_dir.mkdir(parents=True)
    en_dir.mkdir(parents=True)
    (zh_dir / "01-5分钟快速体验.md").write_text("# zh\n", encoding="utf-8")
    (en_dir / "01-quick-start.md").write_text("# en\n", encoding="utf-8")
    issues = lint_docs.check_bilingual_parity(
        tmp_path / "zh-CN", tmp_path / "en",
        mapping={"00-入门/01-5分钟快速体验.md": "00-getting-started/01-quick-start.md"},
    )
    assert not issues


def test_finds_residual_chinese_in_en(tmp_path: Path) -> None:
    """English doc containing CJK chars (outside code blocks) is flagged."""
    en_file = tmp_path / "doc.md"
    en_file.write_text(
        "# Title\n\nThis is mostly English, but 这里有中文 still.\n",
        encoding="utf-8",
    )
    issues = lint_docs.check_residual_chinese([tmp_path])
    assert any("doc.md" in str(i.file) for i in issues)


def test_residual_chinese_ignores_code_blocks(tmp_path: Path) -> None:
    """Code-block-only Chinese is OK (translator notes, etc.)."""
    en_file = tmp_path / "doc.md"
    en_file.write_text(
        "# Title\n\n```\n// 中文注释 in code is OK\n```\n",
        encoding="utf-8",
    )
    issues = lint_docs.check_residual_chinese([tmp_path])
    assert not issues
