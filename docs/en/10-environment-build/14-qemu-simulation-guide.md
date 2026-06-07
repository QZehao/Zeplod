> Language: [中文](../../zh-CN/10-环境与构建/14-QEMU仿真运行指南.md) | **English**

# QEMU Simulation Guide

This guide explains how to **build and run** this project on Zephyr **QEMU boards** on **Windows / Linux / macOS** without physical hardware. Use it to verify boot flow, logging, and module initialization quickly.

> **vs. `native_sim`**: `native_sim` / `native_posix` run the kernel as a POSIX host process and **do not work on native Windows** (see [51-unit-testing-ci.md](../50-testing-ci/51-unit-testing-ci.md)). **QEMU** emulates a real CPU architecture and **runs on Windows 11** when QEMU is installed.

---

## 1. Prerequisites

1. Complete [11-environment-setup.md](11-environment-setup.md) (Zephyr SDK, West, `zephyr_config.env`).
2. Install **QEMU** with `qemu-system-*` binaries available.
3. Set **`QEMU_BIN_PATH`** in **`zephyr_config.env`** — CMake resolves QEMU at **configure** time; PATH alone is not enough.

### 1.1 Install QEMU

**Windows (winget)**:

```powershell
winget install SoftwareFreedomConservancy.QEMU
```

Example install path: `C:\Program Files\qemu`.

**Linux** (example):

```bash
sudo apt install qemu-system-arm qemu-system-misc
```

### 1.2 Configure `QEMU_BIN_PATH`

Edit **`zephyr_config.env`** at the repo root:

```bash
QEMU_BIN_PATH=C:/Program Files/qemu
```

`scripts/setup_env.ps1` / `setup_env.sh` exports **`QEMU_BIN_PATH`** and prepends it to PATH.

Verify:

```powershell
. .\scripts\setup_env.ps1
Get-Command qemu-system-riscv32
```

```bash
source scripts/setup_env.sh
which qemu-system-riscv32
```

---

## 2. Quick start (Windows)

Use an **interactive** PowerShell session (QEMU attaches serial to stdio):

```powershell
cd D:\Code\3-Project\zeplod
. .\scripts\setup_env.ps1

.\scripts\run_qemu.ps1
```

Options:

```powershell
.\scripts\run_qemu.ps1 -ListBoards
.\scripts\run_qemu.ps1 -Board qemu_riscv32
.\scripts\run_qemu.ps1 -Board qemu_x86_64

# SMP variant (see §5)
.\scripts\run_qemu.ps1 -Board "qemu_riscv32/qemu_virt_riscv32/smp"

.\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildDir build_qemu
.\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildOnly
west build -t run --build-dir build_qemu_qemu_riscv32
```

**`-Board`** accepts any Zephyr QEMU board name, including qualified names with `/` (SoC / variant). Each board uses a separate build directory (default `build_qemu_<board>`).

**Exit QEMU**: `Ctrl+A`, then `X`.

See [63-scripts-and-tools.md](../60-debugging/63-scripts-and-tools.md) for script details.

---

## 3. Manual build and run

### 3.1 Recommended boards

| Board | Windows | Notes |
|-------|:-------:|-------|
| **`qemu_riscv32`** | ✅ Recommended | Plenty of RAM; app boot verified |
| **`qemu_riscv64`** | ✅ | RISC-V 64-bit virt |
| **`qemu_x86`** / **`qemu_x86_64`** | ✅ | x86 emulation; **`qemu_x86_64`** is 2-core SMP by default |
| **`qemu_cortex_m3`** / **`qemu_cortex_m0`** | ⚠️ | Builds; app may HardFault on ARM M QEMU — see §7.3 |
| **`qemu_kvm_arm64`** | ❌ | Linux KVM only |

Use overlay config **`prj_qemu.conf`** on top of **`prj.conf`** for simulation (reduced RAM/stacks, watchdog off).

### 3.2 Build

```powershell
. .\scripts\setup_env.ps1
west build -b qemu_riscv32 -d build_qemu . -p always `
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
```

```bash
source scripts/setup_env.sh
west build -b qemu_riscv32 -d build_qemu . -p always \
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
```

Expect in the log:

```text
Found devicetree overlay: .../boards/qemu_riscv32.overlay
```

### 3.3 Run

```bash
west build -t run --build-dir build_qemu
```

### 3.4 Other QEMU boards

Change `-b` and use a separate build directory, e.g.:

```bash
west build -b qemu_cortex_m3 -d build_qemu_m3 . -p always \
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
west build -t run --build-dir build_qemu_m3
```

**`boards/qemu_<board>.overlay`** is merged automatically when the board name matches.

---

## 4. Config files

| File | Role |
|------|------|
| **`prj.conf`** | Main Kconfig |
| **`prj_qemu.conf`** | QEMU overlay: smaller heaps/stacks, KV off, single example module, watchdog off |
| **`boards/qemu_*.overlay`** | Board Devicetree overlays (UART, SRAM on some ARM M boards) |

---

## 5. Multi-core (SMP) simulation

For **dual-core SMP** smoke tests, use Zephyr **SMP board variants** (board defconfig sets `CONFIG_SMP=y`, `CONFIG_MP_MAX_NUM_CPUS=2`). This project's `prj.conf` / `prj_qemu.conf` do **not** disable SMP, so these boards enable multi-core automatically.

### 5.1 SMP boards (Windows)

| `west build -b` | Arch | CPUs | Notes |
|-----------------|------|:----:|-------|
| **`qemu_riscv32/qemu_virt_riscv32/smp`** | RISC-V 32 | 2 | **Recommended**; same family as single-core `qemu_riscv32` |
| **`qemu_riscv64/qemu_virt_riscv64/smp`** | RISC-V 64 | 2 | 64-bit virt SMP |
| **`qemu_cortex_a53/qemu_cortex_a53/smp`** | ARM Cortex-A53 | 2 | ARM64 application-class SMP |
| **`qemu_x86_64`** | x86 64 | 2 | SMP enabled in default defconfig (no `/smp` suffix) |

**Single-core only** (not for SMP): `qemu_cortex_m3`, `qemu_cortex_m0`, non-smp `qemu_cortex_a9`, etc.

`qemu_kvm_arm64` can be multi-core but requires Linux KVM — **not on Windows**.

### 5.2 Script

```powershell
.\scripts\run_qemu.ps1 -Board "qemu_riscv32/qemu_virt_riscv32/smp"
.\scripts\run_qemu.ps1 -Board qemu_x86_64
```

### 5.3 Manual build and run

```powershell
west build -b qemu_riscv32/qemu_virt_riscv32/smp -d build_qemu_smp . -p always `
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
west build -t run --build-dir build_qemu_smp
```

Expect:

```text
Board: qemu_riscv32, qualifiers: qemu_virt_riscv32/smp
```

SMP variants need **fully qualified overlay filenames** (Zephyr does not fall back to `qemu_riscv32.overlay`):

| Board | Overlay file |
|-------|----------------|
| `qemu_riscv32/qemu_virt_riscv32/smp` | `boards/qemu_riscv32_qemu_virt_riscv32_smp.overlay` |
| `qemu_riscv64/qemu_virt_riscv64/smp` | `boards/qemu_riscv64_qemu_virt_riscv64_smp.overlay` |
| `qemu_cortex_a53/qemu_cortex_a53/smp` | `boards/qemu_cortex_a53_qemu_cortex_a53_smp.overlay` |

### 5.4 Verification tips

1. **Link smoke** — SMP increases kernel/stack usage; confirm a full link.
2. **Boot log** — version banner and `Application ready` on the console.
3. **Multi-core behavior** (optional) — shell thread list, per-CPU logging, later ztest for locks/IPC/events under SMP.

SMP board defconfigs usually disable `CONFIG_QEMU_ICOUNT` (incompatible with SMP timing). Boot speed may differ from single-core `qemu_riscv32`.

---

## 6. Expected output (excerpt)

On **`qemu_riscv32`**:

```text
[00:00:00.000,000] <inf> example_module_a: Initializing Example Module A...
  Version:       1.0.0
  Target:        qemu_riscv32
[00:00:00.020,000] <inf> app_main: Application ready (modules=1)
```

---

## 7. Troubleshooting

### 7.1 `QEMU-NOTFOUND`

Set **`QEMU_BIN_PATH`** in **`zephyr_config.env`**, run **`setup_env`**, then rebuild with **`-p always`**.

### 7.2 `west build -t run` fails in CI / background

Run in a real terminal with stdio attached.

### 7.3 HardFault on `qemu_cortex_m3`

Upstream `hello_world` works; this app has known issues on ARM Cortex-M QEMU. Prefer **`qemu_riscv32`** on Windows.

### 7.4 `undefined reference to sys_reboot` with `prj_sram.conf`

Use **`prj_qemu.conf`** for QEMU (`CONFIG_SYS_WATCHDOG_ENABLE=n`).

### 7.5 SMP configure fails: `app.overlay` merged

If the log shows `Found devicetree overlay: .../app.overlay` then configure errors, the SMP qualifier did not match a board overlay and Zephyr fell back to **`app.overlay`** (STM32L4 SRAM — invalid on QEMU). Use the SMP overlay files listed in §5.3.

### 7.6 Wrong SMP board name

Use the full qualifier, e.g. `qemu_riscv32/qemu_virt_riscv32/smp` — not `qemu_riscv32_smp`.

### 7.7 `qemu_kvm_arm64`

Requires Linux **KVM**; not available on Windows.

---

## 8. Related docs

- [12-freestanding-app-build.md](12-freestanding-app-build.md)
- [44-devicetree-memory-config.md](../40-app-development/44-devicetree-memory-config.md)
- [53-hardware-testing.md](../50-testing-ci/53-hardware-testing.md)
- [63-scripts-and-tools.md](../60-debugging/63-scripts-and-tools.md)
