# Zephyr 设备树与内存配置手册

本手册说明在本工程（`zephyr_template`）及 Zephyr 通用机制下，**Devicetree Overlay 如何生效**、**主 RAM（`zephyr,sram`）如何影响链接与堆**，以及**多块、不连续内存**在 Zephyr 中的典型处理方式。适用于 Zephyr 4.x，构建系统以 CMake 生成链接脚本为准。

---

## 目录

1. [核心概念](#1-核心概念)
2. [Overlay 文件如何被构建系统发现](#2-overlay-文件如何被构建系统发现)
3. [文件命名、扩展名与目录约定](#3-文件命名扩展名与目录约定)
4. [手动指定 Overlay](#4-手动指定-overlay)
5. [主 RAM：`zephyr,sram` 与链接脚本](#5-主-ramzephyrsram-与链接脚本)
6. [本工程：STM32L4R5ZI 与 `app.overlay`](#6-本工程stm32l4r5zi-与-appoverlay)
7. [非连续或多块物理 RAM](#7-非连续或多块物理-ram)
8. [与 Kconfig 的配合](#8-与-kconfig-的配合)
9. [验证与调试](#9-验证与调试)
10. [常见问题](#10-常见问题)
11. [参考](#11-参考)

---

## 1. 核心概念

### 1.1 板级设备树（Board DTS）

- 每块板在 Zephyr 仓库中有 **`BOARD.dts`**（例如 `boards/st/nucleo_l4r5zi/nucleo_l4r5zi.dts`），由 SoC 的 **`*.dtsi`** 拼出片上外设、内存、`chosen` 等。
- **`chosen`** 中的 **`zephyr,sram`** 指向主 RAM 节点，用于生成 **`CONFIG_SRAM_*`** 与链接器 **`RAM`** 区域；**`zephyr,flash`** 指向 Flash。

### 1.2 Overlay（覆盖层）

- **Overlay** 是在板级 DTS **之上** 追加的片段，语法与 DTS 相同，用于在不改 Zephyr 上游板文件的前提下，覆盖或增删节点。
- 合并顺序由构建系统决定；后应用的 overlay 可覆盖先出现的属性（具体以 Zephyr 文档与实现为准）。

### 1.3 应用配置目录

- 默认 **`APPLICATION_CONFIG_DIR`** 等于**应用源码根目录**（含 `CMakeLists.txt`、`prj.conf` 的目录）。
- 若使用 Sysbuild 等修改了配置目录，以下路径均以实际配置目录为准。

---

## 2. Overlay 文件如何被构建系统发现

Zephyr 通过 CMake 模块 **`configuration_files.cmake`** 填充 **`DTC_OVERLAY_FILE`**。逻辑要点如下（未在命令行预先设置时）：

1. **`${APPLICATION_CONFIG_DIR}/socs/`**  
   按 SoC 限定名查找匹配的 overlay（带 `QUALIFIERS` 等）。

2. **`${APPLICATION_CONFIG_DIR}/boards/`**  
   按**当前板名**（及可选 SoC 限定）查找 **`<board>.overlay`** 或 **`shortened` 命名**（多 SoC 板时要求带 SoC 后缀的命名，见 Zephyr 报错提示）。

3. 若仍**未**得到 `DTC_OVERLAY_FILE`：  
   在 **`APPLICATION_CONFIG_DIR` 根目录** 再按板名规则查找 `*.overlay`。

4. 若仍没有：  
   再专门查找根目录下的 **`app.overlay`**（可配合 `FILE_SUFFIX` 等变体）。

**结论**：不是“放在工程里任意路径都会生效”，而是必须符合 **目录 + 命名 + 扩展名** 规则，或 **显式传入变量**（见下文）。

构建时可在日志中看到类似：

```text
-- Found devicetree overlay: D:/.../zephyr_template/app.overlay
```

---

## 3. 文件命名、扩展名与目录约定

### 3.1 自动发现只认 `.overlay`

- Zephyr 的 **`zephyr_file(CONF_FILES ... DTS ...)`** 对 DTS 类配置只收集 **`.overlay`** 后缀（见 `extensions.cmake` 中说明）。
- 若文件名为 **`boards/overlay.dts`**，**不会**自动作为 overlay 参与合并，除非通过 **`DTC_OVERLAY_FILE`** / **`EXTRA_DTC_OVERLAY_FILE`** 显式指定。

### 3.2 推荐做法

| 用途 | 推荐文件 |
|------|----------|
| 全工程默认、与板无关的通用覆盖 | 应用根目录 **`app.overlay`** |
| 仅针对某块板 | **`boards/<board>.overlay`** 或 SoC 子目录下按 Zephyr 规则命名 |
| 临时试验 | `west build ... -- -DDTC_OVERLAY_FILE=路径` |

### 3.3 `/delete-node/` 的语法位置

- **`/delete-node/ &label;`** 必须写在 **overlay 文件顶层**，**不能**放在 `/ { ... }` 节点内部，否则 Devicetree 解析会报错（`expected node name` 等）。

---

## 4. 手动指定 Overlay

### 4.1 `DTC_OVERLAY_FILE`

- 在 CMake 或 `west build` 中设置 **`DTC_OVERLAY_FILE`** 为**一个或多个** overlay 路径（分号或列表，按环境而定）。
- 一旦设置，**不再**走上一节的自动搜索逻辑（以 Zephyr 当前实现为准；若需叠加，见下条）。

### 4.2 `EXTRA_DTC_OVERLAY_FILE`

- 在自动或手动指定的 overlay **之后**再合并，适合“追加一层覆盖”而不改原 `DTC_OVERLAY_FILE` 列表。

### 4.3 与 Sysbuild

- 多镜像工程时，变量可能来自 Sysbuild 的 **全局/本地** 传递；若 overlay 未生效，检查 `APPLICATION_CONFIG_DIR` 与 `zephyr_get(... SYSBUILD ...)` 是否覆盖。

---

## 5. 主 RAM：`zephyr,sram` 与链接脚本

- **`chosen { zephyr,sram = &sram0; }`** 指向的 **`memory`** 节点，其 **`reg`** 的 `基址 + 长度` 决定：
  - Kconfig 中的 **`CONFIG_SRAM_BASE_ADDRESS`**、**`CONFIG_SRAM_SIZE`**（单位通常为 KB）；
  - 链接脚本中的 **`RAM`** 区域（`.data`、`.bss`、默认内核堆、多数栈等）。
- Zephyr 4.x 下链接脚本由 **CMake/模板生成**，**不要**沿用旧教程里手写 **`INCLUDE linker-base.ld`** 且路径相对于构建目录的方式，除非完全遵循当前 Zephyr 的 `HAVE_CUSTOM_LINKER_SCRIPT` 与模板要求。

---

## 6. 本工程：STM32L4R5ZI 与 `app.overlay`

### 6.1 SoC 默认 DTS 中的拆分

STM32L4R5 系列在 Zephyr DTS 中常将片上 SRAM 拆成多个 **`memory`** 节点（例如仅 `sram0` 作为 `zephyr,sram` 的 192KB，其余为 `zephyr,memory-region` 等）。**若只使用 192KB 主区**，在开启较大 **`CONFIG_HEAP_MEM_POOL_SIZE`** 与多线程栈时，**RAM 区域**可能链接失败（`region RAM overflowed`）。

### 6.2 本仓库的做法

在 **`app.overlay`** 中：

- 在**顶层** `/delete-node/` 删除 SoC 默认的 **`sram0`、`sram1`、`sram2`** 节点；
- 重新定义 **`sram0`**：`0x20000000` 起 **640 KiB（`0xA0000`）** 连续区域，与 RM0432 中 SRAM1+SRAM2+SRAM3 在地址空间上的连续映射一致；
- 板级 **`chosen`** 仍指向 **`&sram0`**，无需改 `nucleo_l4r5zi.dts`。

这样链接器中的 **`RAM`** 为 **640KB**，与 `west build` 末尾 **Memory region** 输出一致。

### 6.3 与 `prj_sram.conf` 的关系

- **`prj_sram.conf`** 为**说明性**片段（注释），可配合 **`prj.conf`** 使用；**Heap 大小**等仍由 **`CONFIG_HEAP_MEM_POOL_SIZE`** 等在 **`prj.conf`** 中配置。

---

## 7. 非连续或多块物理 RAM

### 7.1 设备树能描述什么

- 可在 DTS 中定义**多个** **`memory`** 节点，部分可带 **`compatible = "zephyr,memory-region"`** 与 **`zephyr,memory-region = "名字"`**，供 DMA、外设驱动或显式链接段使用。

### 7.2 Zephyr 默认不会“自动合并成一块堆”

- **`k_malloc`** 等使用的**默认内核堆**，通常来自 **`zephyr,sram`** 对应的一段**线性地址范围**（由 **`CONFIG_HEAP_MEM_POOL_SIZE`** 等在**该区域内**分配）。
- **设备树仅描述多块 RAM，并不会自动把散落物理块合成一个统一动态分配池**。

### 7.3 常见策略

| 场景 | 方向 |
|------|------|
| CPU 视角上**地址连续**（如本 L4R5 主 SRAM 区） | 合成一个 **`memory` 节点**（如本工程 `app.overlay`） |
| 多段**地址不连续** | 主镜像只选一块作 **`zephyr,sram`**；其余用 **multi-heap**、**mem_attr**、**链接脚本/重定位** 等分别管理 |
| 仅需某段给 DMA | 单独 **`memory-region`** + 驱动或应用固定缓冲区 |

---

## 8. 与 Kconfig 的配合

- **`CONFIG_HEAP_MEM_POOL_SIZE`**：内核堆大小，**不应超过**主 `RAM` 区域在扣除 `.bss`/栈等之后的实际可用空间（需结合 map 文件评估）。
- **`CONFIG_SRAM_SIZE`**、**`CONFIG_SRAM_BASE_ADDRESS`**：由 Devicetree 生成，**不要**在 `prj.conf` 里手工覆盖，除非明确知道后果。
- 其它栈大小（如 **`CONFIG_MAIN_STACK_SIZE`**、应用模块 Kconfig）与 **总 RAM** 一起影响是否溢出。

---

## 9. 验证与调试

1. **构建日志**  
   - 确认 **`Found devicetree overlay: ...`** 是否包含预期文件。  
   - 链接结束处的 **Memory region** 表：`RAM` 的 **Region Size** 是否为预期（如 640 KB）。

2. **生成树**  
   - 查看 **`build/zephyr/zephyr.dts`** 与 **`include/generated/zephyr/devicetree_generated.h`**，确认 `sram` 节点与 `reg`。

3. **map 文件**  
   - **`build/zephyr/zephyr.map`** 分析各段占用与 heap 位置。

4. **配置变更后**  
   - 若修改 overlay 或 `prj.conf`，建议使用 **`west build ... -p always`** 或清理后重配，避免 CMake 缓存导致旧树残留。

---

## 10. 常见问题

**Q1：`boards/overlay.dts` 写了但没生效？**  
自动发现只认 **`.overlay`**（除非用 **`DTC_OVERLAY_FILE`** 显式指定）。建议改名为 **`boards/<board>.overlay`** 或合并进 **`app.overlay`**。

**Q2：overlay 报 `parse error` / `expected node name`？**  
检查 **`/delete-node/`** 是否在**文件顶层**；检查是否误用了不支持的 **`#include`** 位置（视 Zephyr 版本与预处理流程而定）。

**Q3：仍报 `region RAM overflowed`？**  
确认 overlay 已被合并且 **`RAM` 区域大小已变大**；再减小 `CONFIG_HEAP_MEM_POOL_SIZE`、线程栈或关闭不需要的模块。

**Q4：能否用自定义 `.ld` 替代 overlay？**  
Zephyr 4.x 需启用 **`CONFIG_HAVE_CUSTOM_LINKER_SCRIPT=y`**，且脚本须符合当前内核模板；**优先**用 **Devicetree 描述 `zephyr,sram`**，与官方生成链路一致。

---

## 11. 参考

- [Zephyr Application Development — Devicetree](https://docs.zephyrproject.org/latest/build/dts/index.html)
- [Zephyr Devicetree — Overlays](https://docs.zephyrproject.org/latest/build/dts/index.html)（文档中 “Overlays” 章节）
- 本仓库：**[开发者指南.md](开发者指南.md)**、**[项目配置项说明.md](项目配置项说明.md)**  
- STM32L4R5：ST **RM0432** 存储器映射

---

*文档版本与工程目录结构一致；若升级 Zephyr 大版本，请对照官方 Release Notes 核对 CMake 与 DTS 行为。*
