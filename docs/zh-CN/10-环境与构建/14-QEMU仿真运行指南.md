> 语言: **中文** | [English](../../en/10-environment-build/14-qemu-simulation-guide.md)

# QEMU 仿真运行指南

本文说明如何在 **Windows / Linux / macOS** 上使用 Zephyr 自带的 **QEMU 板型** 编译并运行本工程，无需真实开发板。适用于快速验证启动流程、日志、模块初始化等逻辑。

> **与 `native_sim` 的区别**：`native_sim` / `native_posix` 在 PC 上以 POSIX 方式仿真内核，**Windows 原生环境不支持**（见 [51-单元测试与持续集成说明.md](../50-测试与CI/51-单元测试与持续集成说明.md)）。**QEMU 仿真**则模拟真实 CPU 架构，**Windows 11 可直接运行**（需安装 QEMU）。

---

## 1. 前提条件

1. 已完成 [11-环境搭建与配置指南.md](11-环境搭建与配置指南.md) 中的 Zephyr SDK、West、`zephyr_config.env` 配置。
2. 已安装 **QEMU**（本机需包含 `qemu-system-*` 可执行文件）。
3. 在 **`zephyr_config.env`** 中配置 **`QEMU_BIN_PATH`**（CMake 在**配置阶段**解析 QEMU 路径，仅加入系统 PATH 不够）。

### 1.1 安装 QEMU

**Windows（推荐 winget）**：

```powershell
winget install SoftwareFreedomConservancy.QEMU
```

默认安装路径示例：`C:\Program Files\qemu`。

**Linux**（示例）：

```bash
sudo apt install qemu-system-arm qemu-system-misc
```

### 1.2 配置 `QEMU_BIN_PATH`

编辑仓库根目录 **`zephyr_config.env`**：

```bash
QEMU_BIN_PATH=C:/Program Files/qemu
```

加载环境后，`scripts/setup_env.ps1` / `setup_env.sh` 会将其写入 **`$env:QEMU_BIN_PATH`** 并追加到 PATH。

验证：

```powershell
# Windows
. .\scripts\setup_env.ps1
Get-Command qemu-system-riscv32
```

```bash
# Linux / macOS
source scripts/setup_env.sh
which qemu-system-riscv32
```

---

## 2. 最快上手（Windows）

在 **可交互的 PowerShell 终端** 中执行（QEMU 串口绑定到 stdio，自动化终端可能无法附着）：

```powershell
cd D:\Code\3-Project\zeplod
. .\scripts\setup_env.ps1

# 默认板型 qemu_riscv32，自动使用 prj.conf + prj_qemu.conf
.\scripts\run_qemu.ps1
```

常用参数：

```powershell
# 列出脚本内置的 QEMU 板型（含 SMP 变体提示）
.\scripts\run_qemu.ps1 -ListBoards

# 指定仿真板型（等同 west build -b <板型>）
.\scripts\run_qemu.ps1 -Board qemu_riscv32
.\scripts\run_qemu.ps1 -Board qemu_x86_64

# 指定带 qualifier 的板型（多核 SMP，见 §4）
.\scripts\run_qemu.ps1 -Board "qemu_riscv32/qemu_virt_riscv32/smp"

# 自定义构建目录
.\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildDir build_qemu

# 仅编译，稍后手动运行
.\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildOnly
west build -t run --build-dir build_qemu_qemu_riscv32
```

**`-Board`** 接受任意 Zephyr 支持的 QEMU 板名（含 `/` 分隔的 SoC / variant qualifier）。每个板型使用独立构建目录（默认 `build_qemu_<板型>`，特殊字符会替换为 `_`）。

**退出 QEMU**：按 `Ctrl+A`，松开后按 `X`。

脚本说明见 [63-脚本与工具说明.md](../60-调试与排错/63-脚本与工具说明.md)。

---

## 3. 手动编译与运行

### 3.1 推荐板型与配置

| 板型 | Windows 仿真 | 说明 |
|------|:------------:|------|
| **`qemu_riscv32`** | ✅ 推荐 | 内存充裕，本应用已验证可正常启动 |
| **`qemu_riscv64`** | ✅ | RISC-V 64-bit virt |
| **`qemu_x86`** / **`qemu_x86_64`** | ✅ | x86 仿真 |
| **`qemu_cortex_m3`** / **`qemu_cortex_m0`** | ⚠️ | 可编译；当前应用在 ARM M 系 QEMU 上可能 HardFault，见 §7.3 |
| **`qemu_kvm_arm64`** | ❌ | 仅 Linux + KVM，Windows 不可用 |

仿真建议使用叠加配置 **`prj_qemu.conf`**（在 `prj.conf` 基础上削减 RAM/栈，并保持软件看门狗关闭）。

### 3.2 编译

```powershell
# Windows PowerShell
. .\scripts\setup_env.ps1

west build -b qemu_riscv32 -d build_qemu . -p always `
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
```

```bash
# Linux / macOS
source scripts/setup_env.sh

west build -b qemu_riscv32 -d build_qemu . -p always \
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
```

构建日志中应出现：

```text
Found devicetree overlay: .../boards/qemu_riscv32.overlay
```

### 3.3 运行

```bash
west build -t run --build-dir build_qemu
```

等价于由 Zephyr 调用 `qemu-system-<arch>` 加载 `build_qemu/zephyr/zephyr.elf`，串口输出到当前终端。

### 3.4 切换其他 QEMU 板型

将 `-b` 参数改为对应板名即可，例如：

```bash
west build -b qemu_cortex_m3 -d build_qemu_m3 . -p always \
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"
west build -t run --build-dir build_qemu_m3
```

仓库 **`boards/qemu_<板型>.overlay`** 会在匹配板名时自动合并（无需手动指定 `DTC_OVERLAY_FILE`）。部分 ARM M 板 overlay 已扩展 SRAM，详见各 overlay 文件注释。

---

## 4. 配置文件说明

| 文件 | 作用 |
|------|------|
| **`prj.conf`** | 主配置（默认功能集） |
| **`prj_qemu.conf`** | QEMU 叠加：减小堆/栈/事件池，关闭 KV，单示例模块，看门狗保持关闭 |
| **`boards/qemu_*.overlay`** | 板级设备树覆盖（UART、部分板 SRAM 扩展等） |

仅做冒烟、不需完整功能时，可叠加 `prj_qemu.conf`；需要与量产配置一致的行为时，可仅用 `prj.conf`（注意小内存 QEMU 板可能链接或运行失败）。

---

## 5. 多核（SMP）仿真验证

在 QEMU 上做 **双核 SMP** 冒烟时，需选用 Zephyr 提供的 **SMP 板型变体**（板级 defconfig 含 `CONFIG_SMP=y`、`CONFIG_MP_MAX_NUM_CPUS=2`）。本工程 `prj.conf` / `prj_qemu.conf` **未禁用 SMP**，使用下列板型时会自动启用多核。

### 5.1 可用 SMP 板型（Windows）

| `west build -b` 板型 | 架构 | CPU 数 | 说明 |
|----------------------|------|:------:|------|
| **`qemu_riscv32/qemu_virt_riscv32/smp`** | RISC-V 32 | 2 | **推荐**：与单核 `qemu_riscv32` 同族，Win11 已验证单核可启动 |
| **`qemu_riscv64/qemu_virt_riscv64/smp`** | RISC-V 64 | 2 | 64 位 virt SMP |
| **`qemu_cortex_a53/qemu_cortex_a53/smp`** | ARM Cortex-A53 | 2 | ARM64 应用级 SMP |
| **`qemu_x86_64`** | x86 64 | 2 | 默认 defconfig 即 SMP（无需 `/smp` 后缀） |

下列板型为 **单核**，不能用于 SMP 验证：`qemu_cortex_m3`、`qemu_cortex_m0`、`qemu_cortex_a9`（非 smp 变体）等。

`qemu_kvm_arm64` 可多核，但依赖 Linux KVM，**Windows 不可用**。

### 5.2 脚本一键运行（推荐）

```powershell
. .\scripts\setup_env.ps1

# RISC-V 32 双核
.\scripts\run_qemu.ps1 -Board "qemu_riscv32/qemu_virt_riscv32/smp"

# x86_64 双核（板名本身即 SMP）
.\scripts\run_qemu.ps1 -Board qemu_x86_64
```

### 5.3 手动编译与运行

```powershell
. .\scripts\setup_env.ps1

west build -b qemu_riscv32/qemu_virt_riscv32/smp -d build_qemu_smp . -p always `
  -- "-DCONF_FILE=prj.conf;prj_qemu.conf"

west build -t run --build-dir build_qemu_smp
```

构建日志中板型应类似：

```text
Board: qemu_riscv32, qualifiers: qemu_virt_riscv32/smp
```

SMP 变体须使用 **带完整 qualifier 的 overlay 文件名**（Zephyr 不会回退到 `qemu_riscv32.overlay`）：

| 板型 | overlay 文件 |
|------|----------------|
| `qemu_riscv32/qemu_virt_riscv32/smp` | `boards/qemu_riscv32_qemu_virt_riscv32_smp.overlay` |
| `qemu_riscv64/qemu_virt_riscv64/smp` | `boards/qemu_riscv64_qemu_virt_riscv64_smp.overlay` |
| `qemu_cortex_a53/qemu_cortex_a53/smp` | `boards/qemu_cortex_a53_qemu_cortex_a53_smp.overlay` |

单核 `qemu_riscv32` 仍使用 `boards/qemu_riscv32.overlay`。

### 5.4 验证建议

1. **编译冒烟**：确认 SMP 板型能完整链接（多核会增大内核与栈占用）。
2. **启动日志**：串口应正常输出版本横幅与 `Application ready`。
3. **多核行为**（按需深入）：
   - Shell：`kernel cycles`、线程列表（若启用 `CONFIG_SHELL`）；
   - 在业务代码中对不同 CPU 打日志（`arch_curr_cpu()->id` 等）；
   - 后续可叠加专用测试配置或 ztest 用例验证锁、IPC、事件系统在 SMP 下的行为。

SMP 板级 defconfig 通常关闭 `CONFIG_QEMU_ICOUNT`（与多核计时仿真不兼容），启动速度可能与单核 `qemu_riscv32` 略有不同，属正常现象。

---

## 6. 成功输出示例

`qemu_riscv32` 上典型串口输出（节选）：

```text
[00:00:00.000,000] <inf> example_module_a: Initializing Example Module A...
...
  Version:       1.0.0
  Target:        qemu_riscv32
...
[00:00:00.020,000] <inf> app_main: Application ready (modules=1)
```

---

## 7. 常见问题

### 7.1 `QEMU-NOTFOUND` 或找不到 `qemu-system-*`

**原因**：首次 `west build` 时 CMake 未找到 QEMU，缓存为 `QEMU-NOTFOUND`。

**处理**：

1. 在 **`zephyr_config.env`** 中设置正确的 **`QEMU_BIN_PATH`**。
2. 执行 **`. .\scripts\setup_env.ps1`**（或 `source scripts/setup_env.sh`）。
3. **重新配置**构建目录：`west build ... -p always`。

### 7.2 运行后长时间无串口输出

**现象**：构建成功并显示 `Starting QEMU...`，但终端没有任何 Zephyr 启动日志。

**原因**：早期版 `run_qemu.ps1` 用 `2>&1` 缓冲了 `west build -t run` 的全部输出；QEMU 是长期前台进程，日志会被憋到退出后才一次性打印。

**处理**：使用已修复的 `run_qemu.ps1`（直接流式调用 `west`）。若仍无输出，请在本地 **交互式** PowerShell 窗口中运行，不要通过无 TTY 的后台任务或输出重定向启动 QEMU。

### 7.3 `west build -t run` 在非交互环境失败

QEMU 默认将串口绑定到 **stdio**。请在本地 PowerShell / 终端窗口中运行，不要依赖无 TTY 的后台任务。

### 7.4 `qemu_cortex_m3` 上 HardFault / Lockup

Zephyr 官方 `hello_world` 在同板型上可正常运行，本应用在 ARM Cortex-M QEMU 上存在已知兼容问题。**Windows 上请优先使用 `qemu_riscv32`**。若必须在 M 系 QEMU 上调试，请单独排查 ARM 相关初始化路径。

### 7.5 叠加 `prj_sram.conf` 后链接失败 `undefined reference to sys_reboot`

`prj_sram.conf` 启用了软件看门狗相关选项，部分 QEMU 板未提供 `sys_reboot` 实现。仿真请使用 **`prj_qemu.conf`**，其中保持 **`CONFIG_SYS_WATCHDOG_ENABLE=n`**。

### 7.6 SMP 配置失败：合并了 `app.overlay` 或 DTS 报错

**现象**：构建日志出现 `Found devicetree overlay: .../app.overlay`，随后 `Configuring incomplete`。

**原因**：SMP 板型带 qualifier（如 `qemu_virt_riscv32/smp`），Zephyr 只匹配 **`boards/<板名>_<soc>_<variant>.overlay`**。若找不到，会回退到根目录 **`app.overlay`**（本仓库为 **STM32L4R5 SRAM 扩展**，不适用于 QEMU）。

**处理**：确认仓库已包含对应 SMP overlay（见 §5.3 表格），构建日志应显示：

```text
Found devicetree overlay: .../boards/qemu_riscv32_qemu_virt_riscv32_smp.overlay
```

### 7.7 SMP 板型名写错

使用 **`west boards`** 查看完整板名。须写全 qualifier，例如 `qemu_riscv32/qemu_virt_riscv32/smp`，不要写成 `qemu_riscv32_smp`。

### 7.8 `qemu_kvm_arm64`

该板依赖 Linux **KVM**，无法在 Windows 原生环境使用。

---

## 8. 相关文档

- [12-独立应用构建说明.md](12-独立应用构建说明.md) — `west build`、overlay 通用规则
- [44-设备树与内存配置手册.md](../40-应用开发/44-设备树与内存配置手册.md) — `boards/*.overlay` 合并机制
- [53-硬件测试运行指南.md](../50-测试与CI/53-硬件测试运行指南.md) — 真实硬件烧录与测试
- [63-脚本与工具说明.md](../60-调试与排错/63-脚本与工具说明.md) — `run_qemu.ps1` 等脚本
