#!/usr/bin/env python3
"""Resolve framework-only vs framework+app repository layout."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ProjectLayout:
    mode: str  # "framework" | "app"
    scripts_root: Path
    framework_root: Path
    app_root: Path
    work_root: Path

    @property
    def config_file(self) -> Path:
        return self.framework_root / "zephyr_config.env"

    @property
    def tests_dir(self) -> Path:
        return self.framework_root / "tests"

    @property
    def docs_root(self) -> Path:
        return self.framework_root / "docs"

    @property
    def release_dir(self) -> Path:
        return self.work_root / "release"

    @property
    def prj_conf(self) -> Path:
        return self.framework_root / "prj.conf"

    @property
    def proprietary_dir(self) -> Path:
        return self.framework_root / "src" / "proprietary"

    def app_version_file(self) -> Path | None:
        for candidate in (self.app_root / "APP_VERSION", self.framework_root / "APP_VERSION"):
            if candidate.is_file():
                return candidate
        return None

    def package_name(self) -> str:
        cmake = self.app_root / "CMakeLists.txt"
        if cmake.is_file():
            match = re.search(r"project\s*\(\s*([A-Za-z0-9_]+)", cmake.read_text(encoding="utf-8"))
            if match:
                return match.group(1).lower()
        return self.framework_root.name.lower()


def _is_app_wrapper_cmake(path: Path) -> bool:
    if not path.is_file():
        return False
    text = path.read_text(encoding="utf-8", errors="replace")
    if re.search(r"add_subdirectory\s*\(\s*framework\b", text):
        return True
    return "TOPLEVEL_BOOTSTRAP" in text


def resolve_project_layout(scripts_dir: Path | None = None, target: str = "auto") -> ProjectLayout:
    if scripts_dir is None:
        scripts_dir = Path(__file__).resolve().parent
    else:
        scripts_dir = Path(scripts_dir)

    if os.environ.get("ZEPHYR_PROJECT_TARGET"):
        target = os.environ["ZEPHYR_PROJECT_TARGET"]

    scripts_root = scripts_dir.resolve()
    framework_root = scripts_root.parent.resolve()
    parent_root = framework_root.parent.resolve()

    mode = "framework"
    app_root = framework_root
    work_root = framework_root

    if target == "app":
        if not (parent_root / "CMakeLists.txt").is_file():
            raise RuntimeError(f"Target=app but no app wrapper found at {parent_root}")
        mode = "app"
        app_root = parent_root
        work_root = parent_root
    elif target == "framework":
        pass
    else:
        if (
            (framework_root / "prj.conf").is_file()
            and (framework_root / "CMakeLists.txt").is_file()
            and (parent_root / "CMakeLists.txt").is_file()
            and _is_app_wrapper_cmake(parent_root / "CMakeLists.txt")
        ):
            mode = "app"
            app_root = parent_root
            work_root = parent_root

    if os.environ.get("ZEPHYR_APP_ROOT"):
        app_root = Path(os.environ["ZEPHYR_APP_ROOT"]).resolve()
        if mode == "app":
            work_root = app_root

    return ProjectLayout(
        mode=mode,
        scripts_root=scripts_root,
        framework_root=framework_root,
        app_root=app_root,
        work_root=work_root,
    )
