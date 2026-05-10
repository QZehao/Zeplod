> Language: [中文](../../zh-CN/50-测试与CI/51.md) | **English**

# Unit Testing and Continuous Integration Guide

This document describes how to build and run **ztest unit tests** in this repository, and provides an overview of **GitHub Actions / GitLab CI** division of responsibilities. For implementation details, refer to the **`tests/`** directory, **`.github/workflows/ci.yml`**, and **`.gitlab-ci.yml`** (optional).

**To enable CI from scratch, view pipelines, or modify Zephyr versions and board types on GitHub/GitLab**: See **[CI Platform Setup Guide.md](52-ci-platform-setup.md)**.

**Prerequisites**: [Environment Setup Guide.md](../10-getting-started/11-environment-setup.md) · [Developer Quick Start.md](../00-onboarding/04-developer-quick-start.md) - "Testing" section

---

## 1. Unit Tests (`tests/`)

- **Purpose**: Run core logic tests on **host emulation target `native_posix`** without real hardware.
- **Detailed documentation** (directory structure, writing test cases, gcov): Please read **`tests/README.md`** directly.
- **Common commands**:

```bash
west build -b native_posix tests/ --build-dir build_tests
west build -t run --build-dir build_tests
```

- **Note**: The test project uses **`tests/prj.conf`**, separate from the main application's **`prj.conf`**; if you modify the root **`Kconfig`**, the test side reuses the application menu via **`tests/Kconfig`**'s **`rsource`**.

### 1.1 Test Coverage

As of 2026-04-10, tests cover the following core modules:

| Module | Test File | Main Test Content |
|--------|-----------|------------------|
| **Event System** | `test_event_system.c` | Initialization, type registration, subscribe/unsubscribe, event create/release, publish, notify, statistics, queue operations |
| **Event Queue** | `test_event_queue.c` | Initialization, enqueue/dequeue, overflow strategies (DROP_NEWEST/DROP_LOWEST), clear, statistics |
| **Event Dispatcher** | `test_event_dispatcher.c` | Init/start/stop, pause/resume, filters, manual processing, latency statistics, custom configuration |
| **Module Manager** | `test_module_manager.c` | Register/unregister, start/stop all modules, pause/resume, module info, iteration, callbacks, statistics |
| **System Log** | `test_sys_log.c` | Console/memory log, target switching, ring buffer |
| **System Memory** | `test_sys_memory.c` | Initialization, alloc/free, zero-size allocation, statistics |
| **System Timer** | `test_sys_timer.c` | Initialization, create/delete, one-shot/periodic mode, statistics |
| **System Watchdog** | `test_sys_watchdog.c` | Init/start/stop, pause/resume, feed, statistics, simulate expiry, thread monitoring |
| **IPC Service** | `test_ipc_service.c` | Initialization, synchronous calls, start/stop |
| **Example Module A** | `test_example_module_a.c` | Lifecycle, control commands, event handling, concurrent access |
| **Example Module B** | `test_example_module_b.c` | Lifecycle, communication, statistics, event handling |
| **GPIO Module** | `test_example_module_gpio.c` | LED control, button read, blink interval, event handling |
| **UART Module** | `test_example_module_uart.c` | Send/receive, string/binary, statistics, event handling |
| **Multi-Dep Module** | `test_example_module_multi_dep.c` | Dependencies, version, priority, registration ordering |

### 1.2 Running Tests on Hardware

If you need to compile and run tests on real hardware (instead of `native_posix` emulation), see:

👉 **[Hardware Testing Guide.md](53-hardware-testing.md)**

This document covers:
- How to build test firmware for target boards
- Flashing methods (west flash / OpenOCD / IDE)
- Monitoring test output (serial / RTT / GDB)
- Board-level configuration overlays and device tree overrides
- Automated hardware testing configuration in CI

---

## 2. Continuous Integration (GitHub Actions / GitLab)

- **GitHub**: **`.github/workflows/ci.yml`**
- **GitLab**: **`.gitlab-ci.yml`** (aligned with GitHub capabilities, see **[CI Platform Setup Guide.md](52-ci-platform-setup.md)**)

Both typically include (subject to actual YAML):

| Job Type | Typical Content |
|----------|----------------|
| **Code Quality** | **`shellcheck`** (`scripts/*.sh`), **`pre-commit run --all-files`** (including **clang-format**, YAML, trailing whitespace, etc., consistent with **`.pre-commit-config.yaml`**) |
| **Build** | Main project and **`tests/`** build and test run for **`native_posix`**; compile smoke tests for ARM matrix boards (based on **`.github/workflows/ci.yml`**) |

**Version Alignment**: Zephyr container version used by CI is documented in **`Zephyr Version and CI Guide.md`**; local **`ZEPHYR_BASE`** is recommended to be compatible with CI main version line to reduce "passed locally, failed on CI".

---

## 3. Relationship with Main Application

- Tests **share** most implementations under `src/` with the main application, but **do not** link the complete **`app_main`** and all example business modules (based on **`tests/CMakeLists.txt`**).
- When adding modules or code with strong dependencies on board-level peripherals, you need to provide **mock** or conditional compilation in tests to avoid directly accessing non-existent peripherals on **`native_posix`**.

---

## 4. Test Coverage Statistics

### 4.1 Generating Coverage Reports

```bash
# Build tests with coverage
west build -b native_posix tests/ --build-dir build_tests -- -DCMAKE_C_FLAGS="--coverage"

# Run tests
west build -t run --build-dir build_tests

# Generate HTML report
gcovr -r . --html --html-details build_tests/coverage.html
```

### 4.2 Viewing Coverage

Open `build_tests/coverage.html` to view the visual report, including:
- Line coverage per file
- Branch coverage
- Uncovered code lines highlighted

---

## 5. Writing New Tests

### 5.1 Adding Test Files

1. Create `test_<module_name>.c` in `tests/` directory
2. Add source file in `tests/CMakeLists.txt`
3. Ensure tests include:
   - Normal path tests
   - Boundary condition tests
   - Error handling tests
   - Lifecycle tests (init → start → stop → shutdown)

### 5.2 Test Template

```c
#include <zephyr/ztest.h>
#include "your_module.h"

ZTEST(your_module, test_basic_functionality) {
    /* Test normal functionality */
    zassert_equal(your_function(), 0, "Should succeed");
}

ZTEST(your_module, test_edge_cases) {
    /* Test boundary conditions */
    zassert_true(your_function_with_null() != 0, "NULL should return error");
}

ZTEST(your_module, test_error_handling) {
    /* Test error handling */
    zassert_equal(your_function_with_invalid_arg(), -EINVAL, "Should return EINVAL");
}

ZTEST_SUITE(your_module, NULL, NULL, NULL, NULL, NULL);
```

### 5.3 Running Individual Tests

```bash
# Build tests
west build -b native_posix tests/ --build-dir build_tests

# Edit tests/CMakeLists.txt, temporarily comment out other test files, keep only the one to run
# Then rebuild and run
west build -t run --build-dir build_tests
```

---

## 6. References

- **`tests/README.md`**
- **[Hardware Testing Guide.md](53-hardware-testing.md)** - Run tests on real hardware
- **[Zephyr Version and CI Guide.md](../70-release-and-production/72-zephyr-version-ci-guide.md)**
- [Zephyr Testing](https://docs.zephyrproject.org/latest/develop/test/ztest.html)
- [Zephyr Twister](https://docs.zephyrproject.org/latest/develop/test/twister.html)
