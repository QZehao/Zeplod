> 语言: **中文** | English（待译）

# OTA 模块使用说明

本文说明 Zeplod **可选 OTA 模块**（`CONFIG_OTA_MODULE`）的 API、事件契约与测试用法。Phase 1 仅提供 **null 传输**（RAM 模拟镜像），用于 `native_sim` 单元测试；量产 MCUboot 集成将在 Phase 2 提供（《OTA 与存储扩展指南》规划中）。

**相关**： [产品化扩展路线图](../../superpowers/plan/2026-06-13-product-framework-expansion-roadmap.md) · [OTA 实施计划](../../superpowers/plan/2026-06-13-ota-module-implementation-plan.md)

---

## 1. 启用

```bash
west build -b <board> . -- -DEXTRA_CONF_FILE=conf/features/ota.conf
```

`conf/features/ota.conf` 会打开 `CONFIG_OTA_MODULE` 并依赖 `MODULE_MANAGER`、`EVENT_SYSTEM`。

---

## 2. 状态机

| 状态 | 含义 |
|------|------|
| `OTA_STATE_IDLE` | 空闲，可开始升级 |
| `OTA_STATE_DOWNLOADING` | 正在写入镜像块 |
| `OTA_STATE_VERIFYING` | 校验中 |
| `OTA_STATE_READY_REBOOT` | 校验通过，待重启切换 |
| `OTA_STATE_ERROR` | 失败 |

典型流程：`ota_module_begin_update()` → `ota_module_write_chunk()` × N → `ota_module_finish_update()`。

---

## 3. 事件

| 事件 ID | 名称 | 负载 |
|---------|------|------|
| 50 | `EVENT_OTA_STATE_CHANGED` | `ota_progress_t` |
| 51 | `EVENT_OTA_PROGRESS` | `ota_progress_t`（与 STATE_CHANGED 同步发布） |

```c
typedef struct {
    ota_state_t state;
    uint8_t     percent;
    int         error_code;
} ota_progress_t;
```

订阅示例见 `tests/test_ota_module.c`。

---

## 4. Shell

在 `CONFIG_APP_ENABLE_SHELL` 与 `CONFIG_OTA_MODULE` 同时启用时：

```
ota status   # 打印当前状态
ota begin    # 开始 null 传输下载（演示）
ota abort    # 中止会话并回到 idle
```

---

## 5. 传输层扩展

实现 `ota_transport_ops_t` 并注入（Phase 2 将提供 MCUboot 后端）：

- `open` / `write_chunk` / `verify` / `close` / `abort`

Phase 1 内置 `ota_transport_null_get()` 供测试使用。

---

## 6. 错误码

| 宏 | 值 | 含义 |
|----|-----|------|
| `APP_ERR_OTA_INVALID_STATE` | -20 | 当前状态不允许该操作 |
| `APP_ERR_OTA_TRANSPORT` | -21 | 传输层 open/verify 失败 |

---

## 7. 测试

```bash
# Linux / WSL（native_sim）
west build -b native_sim tests/ --build-dir build_tests \
  -- -DEXTRA_CONF_FILE="prj.conf;prj_ota.conf"
west build -t run --build-dir build_tests
```

`prj_ota.conf` 会关闭 `CONFIG_OTA_MODULE_AUTOINIT`，避免与用例内手动 init 冲突。

用例：`tests/test_ota_module.c`（传输、状态机、事件、端到端流程）。
