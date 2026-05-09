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
