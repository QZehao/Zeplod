# AGENTS.md - Zephyr 事件驱动项目模板

本文件为在此仓库中运行的 AI 编码代理提供操作指南。

---

## 1. 构建命令

### 前置条件
- Zephyr SDK (0.16.x+)
- CMake (3.20.0+)
- West (Zephyr 构建工具)
- Python 3.8+

设置 `ZEPHYR_BASE` 环境变量或在仓库根目录配置 `zephyr_config.env`。
配置文件模板：`zephyr_config.env.template` → 复制为 `zephyr_config.env` 后编辑路径。

### 主应用程序构建
```bash
# 为目标板构建
west build -b <board> .

# 构建主机仿真（Zephyr 4.x 用 native_sim；CI 3.6 用 native_posix）
west build -b native_sim .
# west build -b native_posix .

# 使用自定义配置叠加文件构建
west build -b <board> . -- -DCONF_FILE="prj.conf;prj_example_module_ipc.conf"

# 清理并重新构建
west build -t pristine
west build -b <board> .
```

### 单元测试（native_sim / native_posix）
```bash
# 先激活环境（或直接用 run_tests，其内会自动 source setup_env）
source scripts/setup_env.sh     # Linux/macOS/WSL
# .\scripts\setup_env.ps1       # Windows PowerShell

# 推荐：自动激活环境 + 选择板型（native_sim 优先）
./scripts/run_tests.sh          # Linux/macOS/WSL
# .\scripts\run_tests.ps1       # Windows

# 手动（Zephyr 4.x）
west build -b native_sim tests/ --build-dir build_tests
west build -t run --build-dir build_tests

# CI 对齐（Zephyr 3.6 容器）
west build -b native_posix tests/ --build-dir build_tests
west build -t run --build-dir build_tests

# 全量 IPC + Slab（native_sim）
west build -b native_sim tests/ --build-dir build_tests -- -DCONF_FILE="prj_native_sim.conf"
west build -t run --build-dir build_tests

# 带覆盖率运行（将板型换为当前环境可用项）
west build -b native_sim tests/ --build-dir build_tests -- -DCMAKE_C_FLAGS="--coverage"
west build -t run --build-dir build_tests
gcovr -r .. --html --html-details coverage.html
```

### 烧录与监控
```bash
west flash          # 烧录到目标板
west console        # 监控串口输出
```

---

## 2. 代码质量工具

### 格式化代码（clang-format）
格式化配置：`.clang-format`（仓库根目录，基于 LLVM 风格）

```bash
# 格式化单个文件
clang-format -i src/path/to/file.c

# 格式化所有源文件（通过 pre-commit）
pip install pre-commit && pre-commit run --all-files
```

### 静态分析（clang-tidy）
分析配置：`.clang-tidy`（仓库根目录）

```bash
# 需要从构建生成的 compile_commands.json
clang-tidy -p build src/core/event_system.c
```

### Pre-commit 钩子
配置于 `.pre-commit-config.yaml`：
- trailing-whitespace（尾随空白）
- end-of-file-fixer（文件末尾修复）
- check-yaml（YAML 检查）
- clang-format（用于 C/C++ 文件）

安装：`pip install pre-commit && pre-commit install`

---

## 3. 代码风格规范

### 总体规则
- **语言**：C（Zephyr RTOS）
- **标准**：C11（`<stdint.h>`、`<stdbool.h>` 等）
- **缩进**：4 空格（不使用 Tab）
- **列宽限制**：120 字符
- **换行符**：LF（Unix 风格）

### 文件头部模板
```c
/**
 * @file <filename>
 * @brief <简要描述>
 * @author zeh (china_qzh@163.com)
 * @version X.Y
 * @date YYYY-MM-DD
 *
 * @par 修改日志:
 *    Date         Version        Author          Description
 *  YYYY-MM-DD     X.Y            name           初始版本
 */
```

### 头文件保护宏
```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H
// ... 内容 ...
#endif /* MODULE_NAME_H */
```

### 头文件包含顺序
1. `<zephyr/...>`（Zephyr 头文件 - 最高优先级）
2. `<...>`（标准库头文件）
3. `"..."`（本地头文件）

### 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 全局变量 | `g_<name>` | `g_event_system` |
| 静态变量 | `g_<name>` | `g_event_msgq_buffer` |
| 常量/宏 | `UPPER_SNAKE_CASE` | `CONFIG_EVENT_QUEUE_SIZE` |
| 函数 | `snake_case` | `event_system_init()` |
| 类型/结构体 | `snake_case_t` | `event_type_t`、`event_status_t` |
| 枚举值 | `PREFIX_VALUE` | `EVENT_PRIORITY_NORMAL` |
| 模块接口 | `DECLARE_MODULE_INTERFACE(name)` | `DECLARE_MODULE_INTERFACE(my_module)` |
| 模块函数 | `<name>_<action>` | `my_module_init()`、`my_module_start()` |

### 函数注释风格
```c
/**
 * @brief 简要描述
 *
 * 详细描述（如果需要）。
 *
 * @param param_name 参数描述
 * @return 返回值描述
 * @note 重要注意事项
 */
return_type function_name(param_type param_name) {
    // ...
}
```

### 大括号风格
```c
// K&R 风格 - 左大括号在同行
if (condition) {
    do_something();
} else {
    do_something_else();
}
```

### 错误处理
- 使用枚举错误码（如 `event_status_t`、`APP_ERR_*`）
- 返回负数 errno 风格值：`-EINVAL`、`-ENOMEM` 等
- 成功返回 `EVENT_OK`（0）
- 检查所有返回值

```c
event_status_t result = event_publish(&my_event);
if (result != EVENT_OK) {
    LOG_ERR("Failed to publish event: %d", result);
    return result;
}
```

### 创建新模块步骤

1. **定义模块优先级**（在 `src/app/app_config.h`）：
```c
#define APP_INIT_PRIO_MODULE_MINE  60  // 在 MODULE_MGR(54) 和 APP_FINAL(99) 之间
```

2. **创建模块头文件**（`src/modules/my_module.h`）：
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

3. **实现模块**（`src/modules/my_module.c`）：
```c
#include "my_module.h"
#include "app_config.h"

static my_module_config_t g_config;

int my_module_init(void *config) {
    if (config != NULL) {
        g_config = *(my_module_config_t *)config;
    }
    return 0;
}

int my_module_start(void) {
    return 0;
}

int my_module_stop(void) {
    return 0;
}

int my_module_shutdown(void) {
    return 0;
}

void my_module_on_event(const event_t *event, void *user_data) {
    // 处理事件
}

module_status_t my_module_get_status(void) {
    return MODULE_STATUS_RUNNING;
}

int my_module_control(int cmd, void *arg) {
    return 0;
}

DECLARE_MODULE_INTERFACE(my_module);

// 注册模块
static int my_module_auto_register(void) {
    uint32_t id;
    return module_manager_register(&my_module_interface, &g_config, &id) ? -EIO : 0;
}
SYS_INIT(my_module_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_MINE);
```

4. **在 CMakeLists.txt 添加源文件**（按字母顺序添加到对应位置）

### 事件系统使用模式
```c
// 订阅事件
event_subscribe(EVENT_TYPE_SENSOR_DATA, my_callback, user_data, &subscriber_id);

// 发布事件
event_publish_copy(EVENT_TYPE_SENSOR_DATA, EVENT_PRIORITY_NORMAL, &data, sizeof(data));

// ISR 中发布事件
event_publish_from_isr(&my_event);
```

**自动初始化（标准版，默认启用）**

| 阶段 | SYS_INIT 回调 | 优先级 |
|------|----------------|--------|
| 事件系统 init+start | `event_compat_auto_init` | `APP_INIT_PRIO_EVENT_SYS` (40) |
| 分发器 init+start | `event_dispatcher_auto_init` | `APP_INIT_PRIO_DISPATCHER` (45) |

- 链接了 autoinit 时，应用层**不要**再手动 `event_dispatcher_init()` / `event_dispatcher_start()`，以免重复创建分发线程。
- 单元测试若需独占队列消费者，应 `event_system_shutdown()` 收尾（见 `test_event_dispatcher.c`）。
- `CONFIG_EVENT_QUEUE_OVERFLOW_BLOCK=y` 时，`event_publish` 入队使用 `K_FOREVER` 阻塞等待空位；ISR 仍使用 `K_NO_WAIT`。

---

## 4. 项目结构

```
zephyr_template/
├── APP_VERSION                 # 应用语义化版本（X.Y.Z）
├── CMakeLists.txt              # 构建配置
├── Kconfig                     # 应用 Kconfig（含事件/模块/IPC 等）
├── Kconfig_proprietary         # 商业模块 Kconfig
├── prj.conf                    # 默认 Zephyr 配置（最小配置，商用模块默认禁用）
├── prj_example_*.conf          # 叠加配置示例
├── app.overlay                 # 通用设备树覆盖
├── west.yml                   # West 清单
├── zephyr_config.env           # 本地路径配置（勿提交）
├── .clang-format              # 代码格式化配置
├── .clang-tidy                # 静态分析配置
├── .pre-commit-config.yaml    # pre-commit 钩子配置
├── boards/
│   └── overlay.dts            # 通用设备树覆盖
├── src/
│   ├── core/                  # 事件系统核心
│   │   ├── event_system.c/h
│   │   ├── event_queue.c/h
│   │   ├── event_dispatcher.c/h
│   │   └── event_system_compat.c/h
│   ├── services/              # 系统服务
│   │   ├── sys_log.c/h
│   │   ├── sys_memory.c/h
│   │   ├── sys_watchdog.c/h
│   │   └── sys_timer.c/h
│   ├── modules/              # 业务模块
│   │   ├── module_base.h
│   │   ├── module_manager.c/h
│   │   ├── module_manager_compat.c/h
│   │   ├── ipc_service/      # Thread IPC 服务
│   │   ├── example_module_a.c/h
│   │   ├── example_module_b.c/h
│   │   ├── example_module_gpio.c/h
│   │   ├── example_module_uart.c/h
│   │   ├── example_module_ipc.c/h
│   │   └── example_module_multi_dep.c/h
│   ├── app/                  # 应用层
│   │   ├── app_main.c/h
│   │   ├── app_config.h
│   │   ├── app_version.c/h
│   │   └── app_kv.c/h
│   └── proprietary/         # 商业闭源模块
├── tests/                    # ztest 单元测试
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── prj.conf
│   ├── test_event_system.c
│   ├── test_event_queue.c
│   ├── test_event_dispatcher.c
│   ├── test_module_manager.c
│   ├── test_sys_memory.c
│   ├── test_sys_timer.c
│   ├── test_sys_watchdog.c
│   ├── test_sys_log.c
│   └── test_ipc_service.c
├── docs/                     # 文档
├── scripts/                   # 工具脚本
└── copywriting/              # 版权文件
```

---

## 5. 模块系统

### 模块生命周期
`init → start → run → stop → shutdown`

### 模块接口（module_base.h）
```c
typedef struct {
    const char*       name;
    uint32_t          version;
    module_priority_t priority;
    const char* const* depends_on;  // NULL 结尾的模块名数组
    int (*init)(void* config);
    int (*start)(void);
    int (*stop)(void);
    int (*shutdown)(void);
    module_event_handler_t on_event;
    module_status_t (*get_status)(void);
    int (*control)(int cmd, void* arg);
} module_interface_t;
```

### SYS_INIT 优先级（app_config.h）
数值越小越早执行，同级别按文件添加顺序：
- `APP_INIT_PRIO_APP_CB` = 10
- `APP_INIT_PRIO_APP_KV` = 11
- `APP_INIT_PRIO_SYS_LOG` = 20
- `APP_INIT_PRIO_SYS_MEM` = 30
- `APP_INIT_PRIO_EVENT_SYS` = 40
- `APP_INIT_PRIO_DISPATCHER` = 45
- `APP_INIT_PRIO_SYS_TIMER` = 50
- `APP_INIT_PRIO_SYS_WDT` = 52
- `APP_INIT_PRIO_MODULE_MGR` = 54
- `APP_INIT_PRIO_MODULE_A` = 60
- `APP_INIT_PRIO_MODULE_B` = 61
- `APP_INIT_PRIO_MODULE_GPIO` = 62
- `APP_INIT_PRIO_MODULE_UART` = 63
- `APP_INIT_PRIO_MODULE_IPC` = 64
- `APP_INIT_PRIO_MODULE_MULTI` = 65
- `APP_INIT_PRIO_APP_FINAL` = 99

---

## 6. 配置（Kconfig）

- **位置**：根目录 `Kconfig` + `src/modules/ipc_service/Kconfig` + `Kconfig_proprietary`
- **配置文件**：`prj.conf`、`prj_*.conf`（叠加配置）
- **设备树**：`boards/overlay.dts`、`boards/<board>.overlay`、`app.overlay`

### 常用 Kconfig 选项
```
CONFIG_EVENT_SYSTEM=y
CONFIG_EVENT_QUEUE_SIZE=64
CONFIG_EVENT_MAX_SUBSCRIBERS=16
CONFIG_EVENT_DISPATCHER_STACK_SIZE=2048
CONFIG_EVENT_DISPATCHER_PRIORITY=5

CONFIG_MODULE_MANAGER=y
CONFIG_MAX_MODULES=16
CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES=n

CONFIG_SYS_LOG_LEVEL=3
CONFIG_SYS_MEMORY_POOL_SIZE=8192
CONFIG_SYS_WATCHDOG_ENABLE=y
CONFIG_SYS_WATCHDOG_TIMEOUT_MS=5000

# 商用模块默认禁用，按需在 prj_*.conf 中启用
# CONFIG_THREAD_IPC_SERVICE=n
# CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE=n
# CONFIG_APP_KV_PERSIST=n
```

---

## 7. 持续集成（CI）

### GitHub Actions（`.github/workflows/ci.yml`）
- 代码质量：ShellCheck、pre-commit
- 构建：native_posix（CI Zephyr 3.6）、ARM 板矩阵；本机 Zephyr 4.x 推荐 native_sim
- 测试：native_posix ztest（CI）；本机可用 `scripts/run_tests.sh`
- 文档：Doxygen 生成

### GitLab CI（`.gitlab-ci.yml`）
GitHub Actions 的镜像。

---

## 8. 提交信息格式（Conventional Commits）

```
<type>(<scope>): <subject>

<body>

<footer>
```

### 类型说明
| 类型 | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档变更 |
| `style` | 代码格式（不改逻辑） |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `build` | 构建系统 |
| `ci` | CI/CD |
| `chore` | 其他 |

### 示例
```bash
git commit -m "feat(event): 增加 EVENT_TYPE_SENSOR_DATA 类型"
git commit -m "fix(module): 修复模块启动顺序错误"
git commit -m "docs(ci): 更新 CI 板型配置说明"
```

---

## 9. 关键 Zephyr API

- `k_msgq` - 消息队列
- `k_mutex` - 互斥锁
- `k_timer` - 定时器
- `LOG_MODULE_REGISTER()` - 日志模块注册
- `SYS_INIT()` - 初始化
- `DEVICE_DT_GET()` - 设备树
- `k_malloc()` / `k_free()` - Zephyr 内存分配

---

## 10. 重要注意事项

1. **禁止使用类型压制** - 这是 C 语言，不是 TypeScript
2. **必须检查返回值** - 错误处理是强制要求
3. **使用 Zephyr 内存分配** - 使用 `k_malloc()`/`k_free()` 而不是标准库
4. **线程安全** - 共享数据使用互斥锁/原子操作
5. **ISR 安全函数** - 在中断上下文使用 `event_publish_from_isr()`
6. **模块隔离** - 模块间通过事件通信，不使用直接调用
7. **版本文件** - 使用 `APP_VERSION`（不要用 `VERSION`，与 Zephyr 冲突）
