# Zephyr 事件驱动项目模板

基于 Zephyr RTOS 的高性能、实时、事件驱动应用程序模板。此模板使用发布 - 订阅模式，为构建可扩展的模块化嵌入式应用提供了坚实的基础。

> 📺 **视频教程**：关注微信公众号 **硬核嵌入式**，正在连载本框架的详细教程！  
> 💬 **技术交流**：加微信/QQ群 [待补充]，与 500+ 嵌入式开发者交流

---

## 🚀 新人从这里开始

| 我想... | 阅读文档 | 预计时间 |
|---------|----------|----------|
| **5 分钟看看效果** | [docs/5 分钟快速体验.md](docs/5 分钟快速体验.md) | 5 分钟 |
| **正式搭建环境** | [docs/环境搭建与配置指南.md](docs/环境搭建与配置指南.md) | 1-2 小时 |
| **开始写代码** | [docs/开发者入门指南.md](docs/开发者入门指南.md) | 1 小时 |
| **控制硬件（LED）** | [docs/从零到 Blink LED.md](docs/从零到 Blink LED.md) *(待创建)* | 30 分钟 |
| **查术语** | [docs/术语速查卡片.md](docs/术语速查卡片.md) | 随时 |
| **浏览全部文档** | [docs/文档索引.md](docs/文档索引.md) | - |

> 💡 **提示**：第一次接触？从 **[5 分钟快速体验](docs/5 分钟快速体验.md)** 开始，无需开发板即可在 PC 上运行！

---

## 特性

- **事件驱动架构**：基于发布 - 订阅模式的核心事件系统
- **模块化设计**：动态模块注册和生命周期管理
- **实时性能**：基于优先级的调度，可配置的事件分发
- **系统服务**：集成的日志、内存管理、看门狗和定时器服务
- **可扩展性**：易于扩展的模块接口，用于添加业务逻辑
- **线程安全**：完整的线程安全操作和正确的同步机制
- **Shell 命令**：内置调试和监控的 Shell 命令
- **版本管理**：完整的软件版本跟踪，包含 Git 信息和编译时间
- **Thread IPC（应用内）**：可选的工作线程 + 分发线程、请求/响应队列，及与事件系统的桥接（见 `docs/Thread_IPC服务使用说明.md`）

## 项目结构

```
zephyr_template/
├── APP_VERSION                 # 应用语义化版本（勿用文件名 VERSION，与 Zephyr 冲突）
├── CMakeLists.txt              # 构建配置（独立应用需 ZEPHYR_BASE 或 zephyr_config.env）
├── Kconfig                     # 应用 Kconfig（含事件/模块/IPC 等）
├── prj.conf                    # 默认 Zephyr 配置（可与 prj_*.conf 合并）
├── .clang-tidy                 # clang-tidy 检查项（可选）
├── .pre-commit-config.yaml     # pre-commit 钩子（可选）
├── prj_example_module_ipc.conf # Thread IPC 示例模块叠加配置示例
├── prj_example_gpio_uart.conf  # GPIO / UART 示例模块叠加配置（可选）
├── README.md
├── LICENSE
├── west.yml                    # 可选 West 清单
├── .github/workflows/ci.yml    # GitHub Actions CI
├── .gitlab-ci.yml              # GitLab CI（可选；步骤见 docs/CI平台配置保姆级手册.md）
├── zephyr_config.env           # 本地路径（由 template 复制生成，勿提交密钥）
├── zephyr_config.env.template
├── boards/
│   └── overlay.dts             # 通用设备树覆盖
├── scripts/                    # 环境脚本、打包、串口工具等
├── tests/                      # ztest 单元测试（native_posix）
│   ├── CMakeLists.txt
│   ├── Kconfig                 # rsource 复用根目录 Kconfig
│   ├── prj.conf
│   └── test_*.c
├── docs/                       # 说明文档（总目录见 docs/文档索引.md）
└── src/
    ├── core/
    │   ├── event_system.c/h
    │   ├── event_queue.c/h
    │   └── event_dispatcher.c/h
    ├── services/
    │   ├── sys_log.c/h
    │   ├── sys_memory.c/h
    │   ├── sys_watchdog.c/h
    │   └── sys_timer.c/h
    ├── modules/
    │   ├── module_base.h
    │   ├── module_manager.c/h
    │   ├── ipc_service/         # Thread IPC 服务实现（Kconfig: THREAD_IPC_SERVICE）
    │   ├── example_module_a.c/h
    │   ├── example_module_b.c/h
    │   ├── example_module_gpio.c/h
    │   ├── example_module_uart.c/h
    │   ├── example_module_ipc.c/h
    │   └── example_module_multi_dep.c/h  # 多依赖示例（Kconfig: EXAMPLE_MODULE_MULTI_DEP）
    └── app/
        ├── app_main.c/h
        ├── app_config.h
        └── app_version.c/h
```

## 从本模板初始化新项目（检查清单）

复制或 fork 本仓库后，建议按顺序完成下列项，便于在任意产品上落地（更细的说明见 **[docs/开发者入门指南.md](docs/开发者入门指南.md#从模板复制后的检查清单)**）。

| 步骤 | 内容 |
|------|------|
| 1 | **west.yml**：将 `revision` 与团队 Zephyr 版本对齐（默认 **v3.6.0**，与 CI 一致）；私有镜像可改 `url`。 |
| 2 | **zephyr_config.env**：由 `zephyr_config.env.template` 复制并填写路径；**勿提交**（已在 `.gitignore` 中忽略）。 |
| 3 | **CMake**：根目录 `CMakeLists.txt` 中 `project(...)` 名称改为你的产品工程名。 |
| 4 | **版本与说明**：按需修改 `APP_VERSION`、README 标题与产品描述。 |
| 5 | **板型与 CI**：`prj.conf`、**`.github/workflows/ci.yml`** / **`.gitlab-ci.yml`** 中 ARM 矩阵 `board` 与目标硬件一致或按需裁剪。 |
| 6 | **示例代码**：`src/modules/example_*` 可删除或替换；同步 `CMakeLists.txt`、Kconfig，以及各模块 **`.c` 的 `SYS_INIT` 注册**与 **`app_config.h` 的 `APP_INIT_PRIO_*`**。 |

> **文档与 CI 的板型示例**：入门文档中可能出现 `nucleo_l4r5zi` 等示例板名；CI 当前固定为若干 Nucleo/Disco 板。**以你手头的 `BOARD` 与 CI 矩阵为准**；若遇 RAM/链接问题，见 **[docs/设备树与内存配置手册.md](docs/设备树与内存配置手册.md)**。

## 快速开始

### 前提条件

- Zephyr SDK（0.16.x 或更高版本）
- CMake（3.20.0 或更高版本）
- Python 3.8+
- West（Zephyr 构建工具）

CI（`.github/workflows/ci.yml`）当前使用 Zephyr **3.6.0** 构建镜像；本地建议使用相同或兼容的 Zephyr 版本，以减少配置差异。

### 项目类型：独立应用程序 (Freestanding Application)

本项目是一个 **Zephyr 独立应用程序**，应用程序和 Zephyr 源代码位于不同的目录中。

```
<home>/
├── zephyrproject/          # Zephyr 工作区
│   ├── .west/
│   ├── zephyr/             # Zephyr 源代码 (ZEPHYR_BASE)
│   ├── modules/            # Zephyr 模块
│   └── ...
└── zephyr_template/        # 应用程序目录 (本目录)
    ├── CMakeLists.txt
    ├── prj.conf
    ├── src/
    └── ...
```

**重要**：构建时需要设置 `ZEPHYR_BASE` 环境变量指向 Zephyr 源代码目录。

### 配置（本地路径）

本项目使用本地 Zephyr SDK 和源代码路径。构建前请进行配置：

#### 方法 1：编辑配置文件

1. 复制 `zephyr_config.env.template` 到 `zephyr_config.env`
2. 编辑 `zephyr_config.env` 中的路径：

```bash
# 编辑此文件，填入您的本地路径
ZEPHYR_SDK_INSTALL_DIR=C:/zephyr-sdk
ZEPHYR_BASE=D:/zephyrproject/zephyr
```

#### 方法 2：运行设置脚本

```bash
# Windows (PowerShell)
.\scripts\setup_env.ps1

# Windows (CMD)
scripts\setup_env.bat

# Linux/macOS
source scripts/setup_env.sh
```

#### 方法 3：设置环境变量

```bash
# Windows (PowerShell)
$env:ZEPHYR_BASE="D:/zephyrproject/zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR="C:/zephyr-sdk"

# Linux/macOS
export ZEPHYR_BASE=/path/to/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk
```

### 构建

```bash
# 构建目标开发板
west build -b <your_board> .

# 构建 native POSIX（用于测试）
west build -b native_posix .

# 使用特定配置文件（可合并多个）
west build -b <your_board> -DCONF_FILE="prj.conf;prj_example_module_ipc.conf" .

# 清理并重新构建
west build -t pristine
west build -b <your_board> .
```

### 烧录

```bash
west flash
```

### 监控输出

```bash
west console
```

### 使用自定义开发板

本项目支持在 `boards/` 目录中添加自定义开发板，无需修改 Zephyr 源代码。

**开发板目录结构**：

```
boards/
└── vendor/
    └── board_name/
        ├── board_name_defconfig
        ├── board_name.dts
        ├── board_name.yaml
        ├── board.cmake
        ├── Kconfig.defconfig
        └── ...
```

**构建命令**：

```bash
# 在根目录 CMakeLists.txt 中取消注释 BOARD_ROOT 后，可将自定义板放在 boards/ 下
# list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/boards)
west build -b vendor/board_name .
```

**设备树覆盖**：

- 通用覆盖：`boards/overlay.dts`
- 开发板特定覆盖：`boards/<board_name>.overlay`
- 使用 FILE_SUFFIX：`boards/<board_name>_<suffix>.overlay`

## 架构概览

### 事件系统

核心事件系统提供：
- **事件类型**：256 个唯一事件类型（0-255）
- **事件优先级**：低、普通、高、关键
- **订阅者**：每个事件类型最多 16 个订阅者
- **队列**：可配置的事件队列（默认 64 个事件）

```c
// 订阅事件
event_subscribe(EVENT_TYPE_SENSOR_DATA, my_callback, user_data, &subscriber_id);

// 发布事件
event_publish_copy(EVENT_TYPE_SENSOR_DATA, EVENT_PRIORITY_NORMAL, &data, sizeof(data));
```

### 模块系统

模块是独立的业务逻辑单元：
- **生命周期**：init → start → run → stop → shutdown
- **注册**：通过模块管理器动态注册；主固件在 **`SYS_INIT(POST_KERNEL, APP_INIT_PRIO_*)`** 中自动注册（见 `src/app/app_config.h` 与各 `example_module_*.c`）
- **事件处理**：自动将事件路由到订阅的模块
- **隔离**：每个模块有自己的状态和配置

```c
// 定义模块接口
DECLARE_MODULE_INTERFACE(my_module);

// 典型：在模块 .c 内用 Zephyr 自动初始化注册（需在 app_config.h 定义 APP_INIT_PRIO_MODULE_*）
static int my_module_auto_register(void) {
    uint32_t id;
    return module_manager_register(my_module_get_interface(), &config, &id) ? -EIO : 0;
}
SYS_INIT(my_module_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_MINE);
```

### 系统服务

| 服务 | 描述 |
|------|------|
| `sys_log` | 统一日志：分级、内存环、控制台 printk；可选 UART 位与控制台共用输出路径；启用 `CONFIG_SEGGER_RTT` 时可写 RTT |
| `sys_memory` | 内存池管理，带分配跟踪 |
| `sys_watchdog` | 硬件/软件看门狗，提高可靠性 |
| `sys_timer` | 高分辨率单次和周期定时器 |

## 配置

### Kconfig 选项

```kconfig
# 事件系统
CONFIG_EVENT_SYSTEM=y
CONFIG_EVENT_QUEUE_SIZE=64
CONFIG_EVENT_MAX_SUBSCRIBERS=16
CONFIG_EVENT_DISPATCHER_STACK_SIZE=2048
CONFIG_EVENT_DISPATCHER_PRIORITY=5

# 模块管理器
CONFIG_MODULE_MANAGER=y
CONFIG_MAX_MODULES=16
# 可选：运行时按 depends_on 拓扑启动/逆序停止（详见 docs/模块系统详细使用说明.md）
# CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES=y
# CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX=16
# CONFIG_MODULE_MANAGER_START_ALL_ABORT_ON_FAILURE=y
# CONFIG_EXAMPLE_MODULE_MULTI_DEP=y

# 系统服务
CONFIG_SYS_LOG_LEVEL=3          # 0=关闭，1=错误，2=警告，3=信息，4=调试
CONFIG_SYS_MEMORY_POOL_SIZE=8192
CONFIG_SYS_WATCHDOG_ENABLE=y
CONFIG_SYS_WATCHDOG_TIMEOUT_MS=5000
```

### 应用配置

编辑 `src/app/app_config.h` 自定义：
- 功能标志（启用/禁用模块和服务）
- 任务优先级和栈大小
- 定时配置
- 事件类型定义

## Shell 命令

应用程序提供以下 Shell 命令用于调试：

```bash
# 应用状态
app status

# 模块信息
app modules

# 事件统计
app events

# 内存统计
app memory

# 日志转储
app log [level]

# 应用键值（字符串 key/value；掉电保存见 prj_app_kv_persist.conf + boards/nucleo_l4r5zi.overlay）
app kv list
app kv set mykey hello world
app kv get mykey
app kv del mykey
app kv clear
app kv save
app kv load

# 帮助
app help
```

## 创建自定义模块

1. 创建模块头文件（`my_module.h`）：

```c
#ifndef MY_MODULE_H
#define MY_MODULE_H

#include "module_base.h"

typedef struct {
    uint32_t param1;
    uint32_t param2;
} my_module_config_t;

int my_module_init(void *config);
int my_module_start(void);
int my_module_stop(void);
int my_module_shutdown(void);
void my_module_on_event(const event_t *event, void *user_data);
module_status_t my_module_get_status(void);
int my_module_control(int cmd, void *arg);

DECLARE_MODULE_INTERFACE(my_module);

#endif
```

2. 实现模块（`my_module.c`）：

```c
#include "my_module.h"

static my_module_cb_t g_my_module;

int my_module_init(void *config) {
    // 初始化模块
    return 0;
}

int my_module_start(void) {
    // 启动模块操作
    return 0;
}

// ... 实现其他接口函数

DECLARE_MODULE_INTERFACE(my_module);
```

3. 在 **`my_module.c`** 中用 **`SYS_INIT`** 注册（并在 **`app_config.h`** 增加 **`APP_INIT_PRIO_MODULE_MINE`**，取值在 **`APP_INIT_PRIO_MODULE_MGR`** 与 **`APP_INIT_PRIO_APP_FINAL`** 之间；若依赖其它已注册模块，优先级数值应**更大**以更晚执行）：

```c
#include "app_config.h"
#include <zephyr/init.h>
#include <errno.h>

static int my_module_auto_register(void) {
    uint32_t id;
    return module_manager_register(&my_module_interface, &config, &id) ? -EIO : 0;
}
SYS_INIT(my_module_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_MINE);
```

多模块依赖、`depends_on` 写法与 Kconfig 开关说明见 **docs/模块系统详细使用说明.md** 中的「应用启动与初始化顺序（Zephyr SYS_INIT）」「运行时依赖」与「配置选项」；多依赖示例源码为 `src/modules/example_module_multi_dep.c`。

## 事件流程

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   模块 A     │────▶│  事件系统     │────▶│   模块 B     │
│  (发布者)    │     │  (分发器)     │     │  (订阅者)    │
└─────────────┘     └──────────────┘     └─────────────┘
                           │
                           ▼
                    ┌─────────────┐
                    │  事件队列    │
                    └─────────────┘
```

## 最佳实践

1. **事件设计**：保持事件小而专注。对大数据负载使用事件数据指针。
2. **模块隔离**：模块不应直接调用其他模块。使用事件进行模块间通信。
3. **优先级分配**：为时间关键事件使用适当的优先级。
4. **内存管理**：始终释放动态分配的事件数据。
5. **错误处理**：在所有模块回调中实现适当的错误处理。
6. **看门狗**：在长时间运行的操作中定期喂看门狗。

## 调试

### 启用调试日志

```kconfig
CONFIG_LOG_DEFAULT_LEVEL_DBG=y
CONFIG_SYS_LOG_LEVEL=4
```

### 检查统计信息

```c
// 事件系统统计
uint32_t total, depth, dropped;
event_get_statistics(&total, &depth, &dropped);

// 模块管理器统计
module_mgr_stats_t stats;
module_manager_get_stats(&stats);
```

### 内存泄漏检测

```c
// 检查泄漏
uint32_t leaks = sys_mem_check_leaks(SYS_MEM_POOL_GENERAL);
if (leaks > 0) {
    sys_mem_dump_allocations(SYS_MEM_POOL_GENERAL);
}
```

## 单元测试

使用 `tests/` 下的 ztest，与主应用共享 `src/` 实现（不链接 `app_main`、示例业务模块）；默认 **开启** `CONFIG_THREAD_IPC_SERVICE` 并编入 `ipc_service` 做烟测，堆大小已加大（见 `tests/prj.conf`）。

```bash
west build -b native_posix tests/ --build-dir build_tests
west build -t run --build-dir build_tests
```

需设置 `ZEPHYR_BASE` 或提供根目录 `zephyr_config.env`。详见 tests/README.md。

## 许可证

本项目采用 **GNU General Public License v3.0 (GPL-3.0)** - 详见 LICENSE 文件。

### 双许可证模式

本项目采用**双许可证**模式：

- **GPL v3**（免费）：个人学习、研究、开源项目可免费使用，但**必须开源你的衍生代码**
- **商业许可证**（付费）：企业用户、闭源商业产品需购买许可证，可闭源商用

📧 获取商业许可证：发送邮件至 [china_qzh@163.com](mailto:china_qzh@163.com)

💼 企业授权价格：
  - 初创企业授权：¥2,999 起
  - 企业授权：¥19,999 起
  - OEM 授权：¥99,999 起

📄 详细条款：详见 [LICENSE_COMMERCIAL.md](LICENSE_COMMERCIAL.md)

> ⚠️ 违反 GPL 条款使用本代码将面临法律风险！购买商业许可证可合法闭源使用。

## 贡献

### 提交代码（Pull Request）

1. Fork 仓库
2. 创建功能分支（例如 `feature/my-feature` 或 `fix/issue-123`）
3. 进行更改，确保构建和测试通过
4. **使用规范的 Commit 格式**（见下文）
5. 提交 Pull Request

**Commit 格式**：采用 Conventional Commits 规范

```
<type>(<scope>): <subject>
```

**常用 type**：
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `style`: 代码格式（不改逻辑）
- `refactor`: 重构
- `perf`: 性能优化
- `test`: 测试相关
- `build`: 构建系统
- `ci`: CI/CD
- `chore`: 其他琐事

**示例**：
```bash
git commit -m "feat(sys_memory): 增加堆泄漏检测功能"
git commit -m "fix(event): 修复事件队列统计错误"
git commit -m "docs(ci): 更新 CI 板型配置说明"
```

**❌ 避免模糊的提交信息**：
```bash
git commit -m "修改"           # 过于模糊
git commit -m "更新代码"       # 没有信息量
git commit -m "修复 bug"       # 未说明修复什么
```

更详细的规范见 **[docs/参与贡献与代码规范.md](docs/参与贡献与代码规范.md)**。

## 支持

如有问题和功能请求，请在项目仓库中提交 Issue。

---

**版本**：1.0.0（单一来源：根目录 `APP_VERSION`；发布前可运行 `python scripts/bump_version.py X.Y.Z` 同步 Doxygen/README）  
**构建类型**：Release/Debug  
**目标**：通用/Zephyr 支持的开发板

## 文档索引

**总目录（推荐阅读顺序、名词表、旧文件名对照）**：[docs/文档索引.md](docs/文档索引.md)

| 文档 | 说明 |
|------|------|
| [docs/文档索引.md](docs/文档索引.md) | **总入口**：学习路径、全部手册列表 |
| [docs/环境搭建与配置指南.md](docs/环境搭建与配置指南.md) | 工具链、路径、验证构建 |
| [docs/独立应用构建说明.md](docs/独立应用构建说明.md) | 独立应用、`ZEPHYR_BASE`、overlay |
| [docs/开发者入门指南.md](docs/开发者入门指南.md) | 日常开发、测试、调试 |
| [docs/Zephyr应用开发与服务指南.md](docs/Zephyr应用开发与服务指南.md) | Zephyr 通用技术与服务开发纲要 |
| [docs/设备树与内存配置手册.md](docs/设备树与内存配置手册.md) | Devicetree、SRAM、`app.overlay` |
| [docs/项目配置项说明.md](docs/项目配置项说明.md) | **Kconfig 与应用配置项集中说明** |
| [docs/事件系统详细使用说明.md](docs/事件系统详细使用说明.md) | 事件 API 与用法 |
| [docs/模块系统详细使用说明.md](docs/模块系统详细使用说明.md) | 模块生命周期、运行时依赖 |
| [docs/Thread_IPC服务使用说明.md](docs/Thread_IPC服务使用说明.md) | Thread IPC 服务 |
| [docs/Thread_IPC模块集成指南.md](docs/Thread_IPC模块集成指南.md) | 在模块中集成 IPC |
| [docs/OTA与存储扩展指南.md](docs/OTA与存储扩展指南.md) | OTA、NVS、低功耗（可选） |
| [docs/版本管理.md](docs/版本管理.md) | 版本号与构建信息 |
| [docs/Zephyr版本与CI说明.md](docs/Zephyr版本与CI说明.md) | 与 CI 镜像版本对齐 |
| [docs/发布检查清单.md](docs/发布检查清单.md) | 发布前检查 |
| [docs/常见问题与故障排除.md](docs/常见问题与故障排除.md) | 构建与环境排错 |
| [docs/烧录与调试快速指南.md](docs/烧录与调试快速指南.md) | 烧录、串口、调试 |
| [docs/脚本与工具说明.md](docs/脚本与工具说明.md) | `scripts/` 脚本说明 |
| [docs/单元测试与持续集成说明.md](docs/单元测试与持续集成说明.md) | ztest 与 CI 概览 |
| [docs/CI平台配置保姆级手册.md](docs/CI平台配置保姆级手册.md) | **GitHub / GitLab** 上启用与维护 CI 的逐步说明 |
| [docs/系统服务使用说明.md](docs/系统服务使用说明.md) | sys_log / sys_memory / sys_timer / sys_watchdog |
| [docs/参与贡献与代码规范.md](docs/参与贡献与代码规范.md) | PR、代码风格与 CI |
| [docs/安全与密钥管理说明.md](docs/安全与密钥管理说明.md) | 密钥、Secret、OTA 签名注意 |
| [tests/README.md](tests/README.md) | 单元测试（详细） |
| [LICENSE](LICENSE) | GPL v3 全文 |
| [LICENSE_COMMERCIAL.md](LICENSE_COMMERCIAL.md) | 商业许可证条款 |
