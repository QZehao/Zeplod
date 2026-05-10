# 单元测试指南

本目录包含 Zephyr 事件驱动项目模板的单元测试（ztest），与主应用共享 `../src/` 下的实现；**不**编译 `app_main` 与示例业务模块。因此**没有**主固件里的 **`SYS_INIT` 启动链**；各用例需自行按依赖调用 `module_manager_init()`、`event_system_init()` 等（与 [模块系统详细使用说明.md](../docs/zh-CN/30-核心模块/32-模块系统详细使用说明.md) 中「注册和使用模块」手写示例一致）。

**仓库级概览**（与 CI 的关系、与主应用差异）：见 **[docs/zh-CN/50-测试与CI/51-单元测试与持续集成说明.md](../docs/zh-CN/50-测试与CI/51-单元测试与持续集成说明.md)**。

默认在 `tests/prj.conf` 中 **开启** `CONFIG_THREAD_IPC_SERVICE`，并链接 `ipc_service/ipc_service.c` 与 `test_ipc_service.c`（烟测）；若需关闭 IPC 以缩短构建，可将该项改为 `n` 并从 `tests/CMakeLists.txt` 中移除对应 `if(CONFIG_THREAD_IPC_SERVICE)` 块内的源文件引用。

## 目录结构

```
tests/
├── CMakeLists.txt
├── Kconfig                 # rsource 复用仓库根目录 Kconfig
├── prj.conf
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

### West

```bash
west build -b native_posix tests/ --build-dir build_tests
west build -t run --build-dir build_tests
```

在 Linux/macOS 上也可直接执行 `build_tests/zephyr/zephyr`。

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

```bash
west build -b native_posix tests/ --build-dir build_tests -- -DCMAKE_C_FLAGS="--coverage"
west build -t run --build-dir build_tests
gcovr -r .. --html --html-details coverage.html
```

## 持续集成

`native_posix` 测试在 GitHub Actions 的 `build-tests` 任务中执行；见 `.github/workflows/ci.yml`。

## 参考文档

- [Zephyr 测试框架](https://docs.zephyrproject.org/latest/develop/test.html)
- [ztest API 参考](https://docs.zephyrproject.org/latest/develop/test/test_api_reference.html)
