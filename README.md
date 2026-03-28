# Zephyr 事件驱动项目模板

基于 Zephyr RTOS 的高性能、实时、事件驱动应用程序模板。此模板使用发布 - 订阅模式，为构建可扩展的模块化嵌入式应用提供了坚实的基础。

## 特性

- **事件驱动架构**：基于发布 - 订阅模式的核心事件系统
- **模块化设计**：动态模块注册和生命周期管理
- **实时性能**：基于优先级的调度，可配置的事件分发
- **系统服务**：集成的日志、内存管理、看门狗和定时器服务
- **可扩展性**：易于扩展的模块接口，用于添加业务逻辑
- **线程安全**：完整的线程安全操作和正确的同步机制
- **Shell 命令**：内置调试和监控的 Shell 命令

## 项目结构

```
zephyr_template/
├── CMakeLists.txt          # 构建配置
├── Kconfig                 # Kconfig 选项
├── prj.conf                # Zephyr 项目配置
├── README.md               # 本文件
├── LICENSE                 # Apache 2.0 许可证
├── .gitignore
├── west.yml                # West 清单（可选）
├── zephyr_config.env       # 本地路径配置
├── zephyr_config.env.template  # 配置模板
├── boards/
│   └── overlay.dts         # 设备树覆盖
├── scripts/
│   ├── setup_env.bat       # Windows CMD 环境设置
│   ├── setup_env.ps1       # Windows PowerShell 环境设置
│   └── setup_env.sh        # Linux/macOS 环境设置
├── docs/
│   └── SETUP_GUIDE.md      # 详细配置指南
└── src/
    ├── core/               # 核心事件系统
    │   ├── event_system.c/h       # 主事件系统
    │   ├── event_queue.c/h        # 事件队列管理
    │   └── event_dispatcher.c/h   # 事件分发器
    ├── services/           # 系统服务
    │   ├── sys_log.c/h            # 日志服务
    │   ├── sys_memory.c/h         # 内存管理
    │   ├── sys_watchdog.c/h       # 看门狗服务
    │   └── sys_timer.c/h          # 定时器服务
    ├── modules/            # 业务模块
    │   ├── module_base.h          # 模块接口基础
    │   ├── module_manager.c/h     # 模块管理器
    │   ├── example_module_a.c/h   # 示例传感器模块
    │   └── example_module_b.c/h   # 示例通信模块
    └── app/                # 应用层
        ├── app_main.c/h           # 主应用程序
        └── app_config.h           # 应用配置
```

## 快速开始

### 前提条件

- Zephyr SDK（0.16.x 或更高版本）
- CMake（3.20.0 或更高版本）
- Python 3.8+
- West（Zephyr 构建工具）

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
```

### 烧录

```bash
west flash
```

### 监控输出

```bash
west console
```

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
- **注册**：通过模块管理器动态注册
- **事件处理**：自动将事件路由到订阅的模块
- **隔离**：每个模块有自己的状态和配置

```c
// 定义模块接口
DECLARE_MODULE_INTERFACE(my_module);

// 注册到模块管理器
module_manager_register(&my_module_interface, &config, &module_id);
```

### 系统服务

| 服务 | 描述 |
|------|------|
| `sys_log` | 统一日志，支持多级和多目标 |
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

3. 在 `app_main.c` 中注册：

```c
module_manager_register(&my_module_interface, &config, &module_id);
```

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

## 许可证

本项目采用 Apache License 2.0 许可证 - 详见 LICENSE 文件。

## 贡献

1. Fork 仓库
2. 创建功能分支
3. 进行更改
4. 运行测试
5. 提交 Pull Request

## 支持

如有问题和功能请求，请在项目仓库中提交 Issue。

---

**版本**：1.0.0  
**构建类型**：Release/Debug  
**目标**：通用/Zephyr 支持的开发板

## 相关文档

- [配置指南](docs/SETUP_GUIDE.md) - 详细的配置和环境设置说明
- [许可证](LICENSE) - Apache 2.0 许可证全文
