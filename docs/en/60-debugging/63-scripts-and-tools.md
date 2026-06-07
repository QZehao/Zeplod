> Language: [中文](../../zh-CN/60-调试与排错/63-脚本与工具说明.md) | **English**

# Scripts and Tools Guide

This page explains the purpose and usage of scripts under `scripts/`.

## 1. Script Groups

| Group | Scripts |
|---|---|
| Environment setup | `setup_env.ps1` / `setup_env.sh` / `setup_env.bat` |
| QEMU simulation | `run_qemu.ps1` |
| Host tests | `run_tests.ps1` / `run_tests.sh` |
| Host sanitizers | `run_sanitizers.ps1` / `run_sanitizers.sh` |
| Twister | `run_twister.ps1` / `run_twister.sh` |
| Unified QA entrypoint | `qa.ps1` / `qa.sh` |
| Preflight | `preflight_host_tests.py` |
| Docs and checks | `generate_docs.ps1` / `generate_docs.sh` / `lint_docs.py` / `check_encoding.py` / `check_script_docs.py` |
| Version and release | `bump_version.py` / `package_release.ps1` / `package_release.sh` |
| Build analysis | `build_all.bat` / `build_all.sh` / `analyze_map.ps1` / `analyze_map.sh` / `analyze_map.bat` |
| Module/config management | `module_config.py` / `proprietary_manage.ps1` / `proprietary_manage.sh` / `proprietary_manage.bat` |

## 2. Prerequisites

1. Prepare `zephyr_config.env` from `zephyr_config.env.template`.
2. Install west/CMake/Python/Zephyr SDK.
3. For `native_sim/native_posix`, run on Linux/WSL (native Windows is blocked by script checks).

## 3. Usage

### 3.1 Environment setup

```powershell
.\scripts\setup_env.ps1
```

```bash
source scripts/setup_env.sh
```

### 3.2 QEMU simulation (Windows)

```powershell
.\scripts\run_qemu.ps1
.\scripts\run_qemu.ps1 -Board qemu_riscv32 -ListBoards
.\scripts\run_qemu.ps1 -Board "qemu_riscv32/qemu_virt_riscv32/smp"
```

`-Board` accepts any Zephyr QEMU board name (including SMP qualifiers). See **[14-qemu-simulation-guide.md](../10-environment-build/14-qemu-simulation-guide.md)** §5 for SMP boards.

Requires **`QEMU_BIN_PATH`** in **`zephyr_config.env`**. See **[14-qemu-simulation-guide.md](../10-environment-build/14-qemu-simulation-guide.md)**.

### 3.3 Host tests

```powershell
.\scripts\run_tests.ps1
```

```bash
./scripts/run_tests.sh
```

Env overrides:
- `ZEPHYR_TEST_BOARD`
- `ZEPHYR_TEST_CONF`
- `ZEPHYR_TEST_BUILD_DIR`

### 3.4 Sanitizers

```powershell
.\scripts\run_sanitizers.ps1
```

```bash
./scripts/run_sanitizers.sh
```

Env overrides:
- `ZEPHYR_SANITIZER=asan|ubsan|asan-ubsan`
- `ZEPHYR_TEST_BOARD`
- `ZEPHYR_TEST_CONF`
- `ZEPHYR_SAN_BUILD_DIR`

### 3.5 Twister

```powershell
.\scripts\run_twister.ps1 -Platform native_sim
```

```bash
./scripts/run_twister.sh
```

Env overrides:
- `ZEPHYR_TWISTER_PLATFORM`
- `ZEPHYR_TWISTER_OUT_DIR`

### 3.6 Preflight

```bash
python scripts/preflight_host_tests.py
```

### 3.7 Unified QA entrypoint

```powershell
.\scripts\qa.ps1 -Mode all
```

```bash
./scripts/qa.sh all
```

Modes: `test`, `san`, `twister`, `all`

### 3.8 Encoding and docs checks

```bash
python scripts/check_encoding.py
python scripts/check_script_docs.py
```

## 4. Platform Notes

- `native_sim/native_posix` are POSIX-host targets.
- On Windows, use **QEMU** (`run_qemu.ps1`, see [14-qemu-simulation-guide.md](../10-environment-build/14-qemu-simulation-guide.md)) or WSL for host tests.
- CI includes preflight, encoding checks, coverage gates, sanitizers, and twister.
