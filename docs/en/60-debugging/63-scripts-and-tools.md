> Language: [中文](../../zh-CN/60-调试与排错/63-脚本与工具说明.md) | **English**

# Scripts and Tools Guide

This page documents the purpose and typical usage of scripts under the repository root **`scripts/`**, helping new members get started quickly. It does **not** constitute a complete tutorial for third-party tools (Zephyr SDK, West, OpenOCD, etc.).

**Related**: [Environment Setup Guide.md](../10-getting-started/11-environment-setup.md) · [Version Management.md](../70-release/71-version-management.md)

---

## 1. Environment Variables (Before Build)

| Script | Platform | Description |
|--------|----------|-------------|
| **`setup_env.ps1`** | Windows PowerShell | Reads from **`zephyr_config.env`** and sets **`ZEPHYR_BASE`**, **`ZEPHYR_SDK_INSTALL_DIR`**, etc. (if implemented) |
| **`setup_env.bat`** | Windows CMD | Same as above, batch version |
| **`setup_env.sh`** | Linux / macOS | `source scripts/setup_env.sh` |

Ensure **`zephyr_config.env`** is prepared (copied from **`zephyr_config.env.template`**) before use.

---

## 2. Version and Release

| Script | Description |
|--------|-------------|
| **`bump_version.py`** | Uniformly bumps **`APP_VERSION`** and version strings in README/Doxyfile etc. (usage: `python scripts/bump_version.py -h` or see repository docs) |
| **`package_release.ps1` / `package_release.sh`** | Packages release artifacts (if enabled in project; follow comments inside the script) |

See **[Version Management.md](../70-release/71-version-management.md)**, **[Release Checklist.md](../70-release/73-release-checklist.md)**.

---

## 3. Documentation (API)

| Script | Description |
|--------|-------------|
| **`generate_docs.ps1`** | Windows: Calls **Doxygen** to generate **`docs/api/html`** |
| **`generate_docs.sh`** | Linux / macOS: Same as above |

Requires **Doxygen** (and optional **Graphviz**) installed locally. Open **`docs/api/html/index.html`** after generation.

---

## 4. Build Utilities

| Script | Description |
|--------|-------------|
| **`build_all.bat` / `build_all.sh`** | Batch builds multiple targets (if specific board list is configured in repository; follow script content) |
| **`analyze_map.ps1` / `.bat` / `.sh`** | Analyzes linker **`zephyr.map`**, assists viewing memory usage (usage in script comments) |

---

## 5. Tools Not Provided with Repository

Some documentation examples may reference commands like **serial monitoring** or **one-click flashing**; if no corresponding file exists under **`scripts/`**, use directly:

- **`west flash`** / **`west debug`** (see **[Flash and Debug Quickstart.md](61-flash-debug-quickstart.md)**)  
- Host-side tools like **pyserial miniterm**, **PuTTY**, etc.

---

*When adding new scripts, please add a line here and include in **[Documentation Index.md](../00-getting-started/02-docs-index.md)** under "All Manuals" or "Other" as appropriate.*
