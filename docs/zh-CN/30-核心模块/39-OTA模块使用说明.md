> 语言: **中文** | English（待译）

# OTA 模块使用说明

本文说明 Zeplod **可选 OTA 模块**（`CONFIG_OTA_MODULE`）的 API、事件契约、传输后端与测试用法。

**相关**： [产品化扩展路线图](../../superpowers/plan/2026-06-13-product-framework-expansion-roadmap.md) · [OTA 实施计划](../../superpowers/plan/2026-06-13-ota-module-implementation-plan.md) · [74-OTA与存储扩展指南](../70-发布与产品化/74-OTA与存储扩展指南.md)

---

## 1. 启用

```bash
# 开发 / ztest（null 传输）
west build -b <board> . -- -DEXTRA_CONF_FILE=conf/features/ota.conf

# MCUboot 传输（须板级 slot 分区）
west build -b <board> . -- -DEXTRA_CONF_FILE=conf/targets/mcuboot.conf
```

`conf/features/ota.conf` 使用 **null 传输**（RAM 模拟）；`conf/targets/mcuboot.conf` 启用 `ota_transport_mcuboot` + `IMG_MANAGER`。

---

## 2. 状态机

| 状态 | 含义 |
|------|------|
| `OTA_STATE_IDLE` | 空闲，可开始升级 |
| `OTA_STATE_DOWNLOADING` | 正在写入镜像块 |
| `OTA_STATE_VERIFYING` | 校验中 |
| `OTA_STATE_READY_REBOOT` | 校验通过，待重启切换 |
| `OTA_STATE_ERROR` | 失败 |

典型流程：`ota_module_begin_update()` → `ota_module_write_chunk()` × N → `ota_module_finish_update()` → `ota_module_request_reboot()`。

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

---

## 4. 传输后端

Kconfig `OTA_TRANSPORT` 选择：

| 选项 | 实现 | 用途 |
|------|------|------|
| `OTA_TRANSPORT_NULL` | `ota_transport_null_get()` | native_sim / ztest |
| `OTA_TRANSPORT_MCUBOOT` | `ota_transport_mcuboot_get()` | `flash_img` 写 secondary slot |

MCUboot 传输要求 **顺序写入**（`offset` 须等于已写字节数）。`finish` 成功后会 `boot_request_upgrade(BOOT_UPGRADE_TEST)`。`abort` 在已写入数据时会擦除 upload slot（`boot_erase_img_bank`）。

升级后配置结构变化时，在固件启动早期调用 `app_kv_run_migrations()`；见 `app_kv.h`。

---

## 5. Shell

在 `CONFIG_APP_ENABLE_SHELL` 与 `CONFIG_OTA_MODULE` 同时启用时：

```
ota status   # 打印当前状态
ota begin    # 开始下载（演示）
ota abort    # 中止会话
```

---

## 6. 错误码

| 宏 | 值 | 含义 |
|----|-----|------|
| `APP_ERR_OTA_INVALID_STATE` | -20 | 当前状态不允许该操作 |
| `APP_ERR_OTA_TRANSPORT` | -21 | 传输层 open/verify 失败 |

---

## 7. 测试

```bash
west build -b native_sim tests/ --build-dir build_tests_ota -p always \
  -- -DCONF_FILE="prj.conf;prj_ota.conf"
west build -t run --build-dir build_tests_ota
```

`prj_ota.conf` 关闭 `CONFIG_OTA_MODULE_AUTOINIT`。用例：`tests/test_ota_module.c`。
