# 单元测试指南

本目录包含 Zephyr 事件驱动项目模板的单元测试（ztest），与主应用共享 `../src/` 下的实现；**不**编译 `app_main` 与示例业务模块。因此**没有**主固件里的 **`SYS_INIT` 启动链**；各用例需自行按依赖调用 `module_manager_init()`、`event_system_init()` 等（与 [模块系统详细使用说明.md](../docs/zh-CN/30-核心模块/32-模块系统详细使用说明.md) 中「注册和使用模块」手写示例一致）。

**仓库级概览**（与 CI 的关系、与主应用差异）：见 **[docs/zh-CN/50-测试与CI/51-单元测试与持续集成说明.md](../docs/zh-CN/50-测试与CI/51-单元测试与持续集成说明.md)**。

## 配置文件

| 文件 | 用途 |
|------|------|
| `prj.conf` | **默认**：精简配置，**`CONFIG_THREAD_IPC_SERVICE=n`**，适合快速冒烟与 CI（Zephyr 3.6 容器仍使用 `native_posix`） |
| `prj_native_sim.conf` | **完整覆盖**：较大堆、Slab、**`CONFIG_THREAD_IPC_SERVICE=y`**，适合 Zephyr 4.x 本机 `native_sim` 全量测试 |

默认 **`tests/prj.conf` 不开启 IPC**，不链接 `test_ipc_service.c`。需要 IPC 烟测时：

```bash
west build -b native_sim tests/ --build-dir build_tests \
  -- -DCONF_FILE="prj_native_sim.conf"
```

或在 `prj.conf` 中将 `CONFIG_THREAD_IPC_SERVICE` 改为 `y`（并确保 `tests/CMakeLists.txt` 中 IPC 源文件条件编译已满足）。

## 主机仿真板型

| Zephyr 版本 | 推荐板型 | 说明 |
|-------------|----------|------|
| 4.x | **`native_sim`** | `native_posix` 已弃用；本机开发优先使用 |
| 3.6（与 CI 容器一致） | **`native_posix`** | GitHub/GitLab CI 当前仍使用该板型 |

可使用仓库脚本自动选择板型（见下方「运行测试」）。

## 目录结构

```
tests/
├── CMakeLists.txt
├── Kconfig                 # rsource 复用仓库根目录 Kconfig
├── prj.conf
├── prj_native_sim.conf     # native_sim 全量配置（可选）
├── test_event_system.c
├── test_event_queue.c
├── test_event_dispatcher.c
├── test_module_manager.c
├── test_sys_memory.c
├── test_sys_timer.c
├── test_sys_watchdog.c
├── test_sys_log.c
└── test_ipc_service.c      # 需 CONFIG_THREAD_IPC_SERVICE=y
```

## 运行测试

### 前提

- 已设置 `ZEPHYR_BASE`，或已在仓库根目录配置 `zephyr_config.env`（与主工程相同）。

### 推荐：使用脚本（自动选择板型）

**Linux / macOS / WSL：**

```bash
./scripts/run_tests.sh
```

**Windows（PowerShell）：**

```powershell
.\scripts\run_tests.ps1
```

脚本会优先使用 `native_sim`（若 `west boards` 列出），否则回退 `native_posix`。环境变量 `ZEPHYR_TEST_BOARD`、`ZEPHYR_TEST_CONF` 可覆盖板型与 `CONF_FILE`。

### West（手动）

Zephyr 4.x（本机）：

```bash
west build -b native_sim tests/ --build-dir build_tests
west build -t run --build-dir build_tests
```

Zephyr 3.6 / CI 对齐：

```bash
west build -b native_posix tests/ --build-dir build_tests
west build -t run --build-dir build_tests
```

全量 IPC + Slab（`native_sim`）：

```bash
west build -b native_sim tests/ --build-dir build_tests \
  -- -DCONF_FILE="prj_native_sim.conf"
west build -t run --build-dir build_tests
```

在 Linux/macOS 上也可直接执行 `build_tests/zephyr/zephyr`。

### 套件间隔离

部分用例依赖全局事件/分发器状态。`test_event_dispatcher.c` 等在 `after_each` 中调用 `event_system_shutdown()` 以清空类型表与分发线程；新增套件时请遵循相同模式，避免跨套件污染。

## 编写新测试

### 测试模板

```c
#include <zephyr/ztest.h>
#include <被测试模块.h>

ZTEST(test_module_name, test_feature)
{
    zassert_equal(expected, actual, "错误消息");
}

ZTEST_SUITE(test_module_name, NULL, NULL, NULL, NULL, NULL);
```

### 常用断言宏

| 宏 | 描述 |
|----|------|
| `zassert_equal(expected, actual, msg)` | 断言相等 |
| `zassert_not_equal(expected, actual, msg)` | 断言不相等 |
| `zassert_true(condition, msg)` | 断言条件为真 |
| `zassert_false(condition, msg)` | 断言条件为假 |
| `zassert_null(pointer, msg)` | 断言指针为空 |
| `zassert_not_null(pointer, msg)` | 断言指针非空 |

## 测试覆盖率

### 使用 gcov

将 `native_sim` 或 `native_posix` 替换为当前环境可用板型：

```bash
west build -b native_sim tests/ --build-dir build_tests -- -DCMAKE_C_FLAGS="--coverage"
west build -t run --build-dir build_tests
gcovr -r .. --html --html-details coverage.html
```

## 持续集成

`native_posix` 测试在 GitHub Actions 的 `build-tests` 任务中执行（Zephyr **3.6.0** 容器）；见 `.github/workflows/ci.yml`。本机 Zephyr 4.x 请使用 `native_sim` 与上文脚本。

## 参考文档

- [Zephyr 测试框架](https://docs.zephyrproject.org/latest/develop/test.html)
- [ztest API 参考](https://docs.zephyrproject.org/latest/develop/test/test_api_reference.html)
