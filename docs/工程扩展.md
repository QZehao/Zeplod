# 模板扩展：OTA、持久化与低功耗

本文档为 **可选** 集成指引，不绑定具体板卡；实现前请对照目标 SoC、分区布局与 Zephyr 版本阅读官方文档。

## 1. OTA / MCUboot

### 1.1 角色分工

在 Zephyr 典型 OTA 方案中，**MCUboot** 是驻留在 Flash 前部的 **二级引导程序**：上电后先运行 MCUboot，由其根据元数据与校验结果决定 **从哪个槽启动** 应用镜像，并在升级流程中完成 **镜像搬运、校验、交换或覆盖**。你的业务固件（本模板中的应用程序）则编译为 **可升级的 signed image**，与 bootloader **分开构建、分开烧录**（或使用 **sysbuild** 一次产出多个镜像）。

官方概念与选项说明见：[MCUboot with Zephyr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcuboot.html)。

### 1.2 工作区中引入 MCUboot 源码

MCUboot 通常作为 **West 工程中的一个 project** 检出到 `bootloader/mcuboot`（路径以你方 `west.yml` 为准）。做法包括：

- 在 **Zephyr 工作区** 的 manifest 中增加 `mcuboot` 仓库，并执行 `west update`；
- 或在你已有 `west.yml` 中 **import** Zephyr 官方 manifest 里已包含的 bootloader 片段（取决于你们工作区组织方式）。

**要点**：应用工程（本仓库）与 MCUboot **不在同一 CMake 工程**里各自编译；若使用 **sysbuild**，由顶层 CMake 同时编排 **bootloader + app** 的镜像与依赖关系。

### 1.3 构建方式：sysbuild 与「双镜像」

- **sysbuild（推荐用于新工程）**  
  Zephyr 的 **系统构建** 可在一次配置中同时构建 **MCUboot** 与 **应用**，并自动处理镜像布局、分区名与部分打包选项。入口与 `sysbuild.cmake` 写法见当前 Zephyr 文档中的 **Sysbuild** 与 **MCUboot** 章节。

- **传统双镜像**  
  分别对 **MCUboot** 与 **应用** 执行 `west build`，再按分区表将 `zephyr.elf` / `zephyr.bin` / signed 产物烧录到对应分区。适合已有脚本或 CI 已固定为两步构建的场景。

无论哪种方式，**Flash 分区表**必须与 **MCUboot 的槽位策略**、**应用链接脚本** 一致。

### 1.4 分区与槽位（slot0 / slot1 / scratch）

常见布局（名称与大小以 SoC 与产品为准）：

| 区域 | 含义（典型） |
|------|----------------|
| **slot0** | 当前运行分区（primary） |
| **slot1** | 升级写入分区（secondary）或镜像 B |
| **scratch** | 部分交换策略（swap）下用于 **临时搬运** 的专用分区；若使用 **overwrite / 单槽** 等策略，可能没有 scratch 或布局不同 |

分区在 **设备树** 中通过 `fixed-partitions` / `partition` 节点描述，并与 `chosen`（如 `zephyr,flash`、分区标签）对应。**不同芯片** Flash 对齐、扇区大小、是否支持原地交换都不同，必须查阅 **该 SoC 的 MCUboot 移植说明** 与 Zephyr board 示例。

**本模板**：`boards/` 下仅提供通用 overlay 时，**不要**直接当作量产分区方案；请为你的板子 **单独写 overlay**，并与 `pm_static.yml`（若使用）或分区生成脚本一致。

### 1.5 应用侧 Kconfig（示意，以当前 Zephyr 版本为准）

符号名会随版本演进，合并前请用 `menuconfig` / 搜索 **IMG_MANAGER**、**MCUboot**、**BOOTLOADER_MCUBOOT** 核对。常见方向：

| 方向 | 说明 |
|------|------|
| **Bootloader 与镜像管理** | 若应用需 **确认/切换/查询** 镜像状态，需打开与 **IMG_MANAGER**、**MCUboot** 相关的选项（具体名称以文档为准）。 |
| **签名与加密** | 启用 **signed** 构建后，需配置 **密钥路径**、**签名算法**（如 `CONFIG_IMG_SIGNING_KEY_FILE` 等，以版本为准）；**密钥与 CI 安全** 勿提交仓库。 |
| **升级后行为** | 如 **test** 模式、**revert**、**confirm** 等，与 MCUboot 的 `bootutil` 行为一致，需在应用侧与文档对齐。 |

**注意**：`CONFIG_BOOTLOADER_MCUBOOT` 一类选项常出现在 **bootloader 工程** 或 **sysbuild 子镜像** 中，应用 `prj.conf` 里未必同名；避免把 bootloader 的选项原样复制到应用。

### 1.6 传输与 mcumgr（SMP）

设备上常通过 **MCUmgr（SMP）** 经 **UART / BLE / UDP** 等传输 **镜像分片**。集成时请关注：

- **栈与线程**：SMP 与传输层会占用 **任务栈**；与本模板事件总线、Thread IPC、大缓冲区并发时，需在 `prj.conf` 中 **加大栈** 或 **限流**。
- **与业务隔离**：建议将 **下载/校验/状态机** 放在独立模块，通过 **事件** 通知其它模块（见第 4 节），避免在回调里长时间阻塞。

参考：[Device Management (mcumgr)](https://docs.zephyrproject.org/latest/services/device_mgmt/index.html)。

### 1.7 设备树 overlay 要点

- 在 **`boards/<vendor>/<board>/`** 或本仓库 **`boards/`** 下为 **你的板子** 增加 `*.overlay`：声明 **分区**、**bootloader 槽**、**存储外设**。
- 与 **MCUboot** 的 **partition 标签** 必须与 `CONFIG_BOOTLOADER_MCUBOOT_*` / 分区生成工具一致。

### 1.8 参考链接

- [MCUboot with Zephyr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcuboot.html)
- [MCUboot sample（Zephyr）](https://docs.zephyrproject.org/latest/samples/subsys/mcuboot/README.html)
- [Device Management (mcumgr)](https://docs.zephyrproject.org/latest/services/device_mgmt/index.html)

---

**小结**：OTA 与 **Flash 布局、签名、引导链** 强相关；本模板只提供**架构与模块边界**上的建议，**量产前**必须在目标硬件上完成 **全链路烧录、回滚、断电恢复** 测试。

---

## 2. NVS / 出厂配置（Settings）

**思路**：使用 [Settings 子系统](https://docs.zephyrproject.org/latest/services/storage/index.html)（底层常为 NVS 或文件），存放校准参数、设备名、计数器等。

**配置片段示例**（具体符号以所用 Zephyr 版本为准）：

```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
```

**应用侧**：`settings_subsys_init()` → `settings_load()` → `settings_save_one()` 等；注意 **磨损均衡** 与 **上电首次默认值**。

---

## 3. 低功耗（PM / Tickless）

**思路**：在可休眠硬件上启用 `CONFIG_PM`、tickless 定时器，并在业务模块中缩短持锁时间、避免在关键路径上长时间阻塞。

**配置方向（概要）**：

- `CONFIG_PM=y`、`CONFIG_PM_DEVICE=y`（若使用设备运行时 PM）
- `CONFIG_TICKLESS_KERNEL=y`（若 SoC/驱动支持）

**与事件系统**：长时间 `k_sleep` 前确认 **看门狗**、**IPC 超时**、**事件队列深度** 是否仍满足实时性要求。

---

## 4. 与本模板事件/模块的关系

- **OTA**：建议在独立模块中处理下载状态机，通过 **事件** 通知其它模块（如重启、版本号展示）。
- **NVS**：初始化宜放在 `app_main` 或专用 `storage` 模块的 `init` 阶段，失败路径需 **日志 + 安全默认配置**。
- **PM**：进入深度睡眠前停止外设、注销不必要的轮询；与 `sys_timer`、Thread IPC 的线程栈需一并评估。

更多架构说明见 [模块系统详细使用说明.md](模块系统详细使用说明.md) 与 [事件系统详细使用说明.md](事件系统详细使用说明.md)。
