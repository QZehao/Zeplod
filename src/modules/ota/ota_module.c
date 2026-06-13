/**
 * @file ota_module.c
 * @brief OTA 模块生命周期与更新流程
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本（Phase 1 null 传输）
 * 2026-06-13       1.1            zeh            幂等 init、锁序、锁外发事件、ERROR 恢复
 * 2026-06-13       1.2            zeh            传输后端 Kconfig 选择；request_reboot
 *
 */

#include <zeplod/ota_module.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <errno.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/lock_order.h>
#include <zeplod/module_manager.h>

LOG_MODULE_REGISTER(ota_module, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    ota_sm_t                  sm;
    const ota_transport_ops_t* transport;
    module_status_t           status;
    size_t                    image_size;
    struct k_mutex            lock;
    bool                      lock_ready;
    bool                      events_registered;
    bool                      transport_open;
} ota_module_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static ota_module_cb_t g_ota;

/* =============================================================================
 * 前置声明
 * ============================================================================= */

static void ota_lock(void);
static void ota_unlock(void);
static void ota_build_progress(ota_progress_t* prog, ota_state_t state, int error_code);
static int  ota_publish_progress(const ota_progress_t* prog);
static void ota_reset_session_locked(void);
static int  ota_transport_fail_locked(int err, ota_progress_t* out_prog);
static int  ota_register_event_types(void);

/* =============================================================================
 * 锁与内部辅助
 * ============================================================================= */

static void ota_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_ota.lock);
    k_mutex_lock(&g_ota.lock, K_FOREVER);
}

static void ota_unlock(void) {
    k_mutex_unlock(&g_ota.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_ota.lock);
}

static uint8_t ota_percent_for_state(ota_state_t state, size_t image_size) {
    const size_t cap = (size_t) CONFIG_OTA_EXPECTED_IMAGE_SIZE;

    switch (state) {
    case OTA_STATE_IDLE:
        return 0U;
    case OTA_STATE_DOWNLOADING:
        if (cap == 0U) {
            return 0U;
        }
        if (image_size >= cap) {
            return 99U;
        }
        return (uint8_t) ((image_size * 100U) / cap);
    case OTA_STATE_VERIFYING:
        return 95U;
    case OTA_STATE_READY_REBOOT:
        return 100U;
    case OTA_STATE_ERROR:
    default:
        return 0U;
    }
}

static void ota_build_progress(ota_progress_t* prog, ota_state_t state, int error_code) {
    prog->state = state;
    prog->percent = ota_percent_for_state(state, g_ota.image_size);
    prog->error_code = error_code;
}

static int ota_publish_progress(const ota_progress_t* prog) {
    event_status_t st;

    st = event_publish_copy(EVENT_OTA_STATE_CHANGED, EVENT_PRIORITY_NORMAL, prog, sizeof(*prog));
    if (st != EVENT_OK) {
        LOG_WRN("OTA state event publish failed: %d", st);
        return -EIO;
    }

    st = event_publish_copy(EVENT_OTA_PROGRESS, EVENT_PRIORITY_NORMAL, prog, sizeof(*prog));
    if (st != EVENT_OK) {
        LOG_WRN("OTA progress event publish failed: %d", st);
        return -EIO;
    }

    return 0;
}

static void ota_reset_session_locked(void) {
    if (g_ota.transport_open && g_ota.transport != NULL && g_ota.transport->abort != NULL) {
        g_ota.transport->abort((ota_transport_ops_t*) g_ota.transport);
    }
    g_ota.transport_open = false;
    ota_sm_init(&g_ota.sm);
    g_ota.image_size = 0U;
}

static int ota_transport_fail_locked(int err, ota_progress_t* out_prog) {
    (void) ota_sm_on_error(&g_ota.sm, err);
    ota_build_progress(out_prog, OTA_STATE_ERROR, err);
    if (g_ota.transport != NULL && g_ota.transport->abort != NULL) {
        g_ota.transport->abort((ota_transport_ops_t*) g_ota.transport);
    }
    g_ota.transport_open = false;
    return err;
}

static int ota_register_event_types(void) {
    event_status_t st;

    if (g_ota.events_registered) {
        return 0;
    }

    st = event_register_type(EVENT_OTA_STATE_CHANGED, "ota_state");
    if (st != EVENT_OK) {
        LOG_ERR("Failed to register EVENT_OTA_STATE_CHANGED: %d", st);
        return -EIO;
    }

    st = event_register_type(EVENT_OTA_PROGRESS, "ota_progress");
    if (st != EVENT_OK) {
        LOG_ERR("Failed to register EVENT_OTA_PROGRESS: %d", st);
        return -EIO;
    }

    g_ota.events_registered = true;
    return 0;
}

/* =============================================================================
 * 模块接口实现
 * ============================================================================= */

int ota_module_init(void* config) {
    int ret;

    ARG_UNUSED(config);

    if (g_ota.status != MODULE_STATUS_UNINITIALIZED) {
        return 0;
    }

    if (!g_ota.lock_ready) {
        k_mutex_init(&g_ota.lock);
        g_ota.lock_ready = true;
    }

    ota_sm_init(&g_ota.sm);
#if IS_ENABLED(CONFIG_OTA_TRANSPORT_MCUBOOT)
    g_ota.transport = ota_transport_mcuboot_get();
#else
    g_ota.transport = ota_transport_null_get();
#endif
    g_ota.image_size = 0U;
    g_ota.transport_open = false;
    g_ota.status = MODULE_STATUS_INITIALIZED;

    ret = ota_register_event_types();
    if (ret != 0) {
        g_ota.status = MODULE_STATUS_UNINITIALIZED;
        return ret;
    }

    LOG_INF("OTA module initialized (%s transport)",
#if IS_ENABLED(CONFIG_OTA_TRANSPORT_MCUBOOT)
            "mcuboot");
#else
            "null");
#endif
    return 0;
}

int ota_module_start(void) {
    LOG_INF("Starting OTA module...");

    ota_lock();
    if (g_ota.status == MODULE_STATUS_UNINITIALIZED) {
        ota_unlock();
        return APP_ERR_INIT;
    }
    if (g_ota.status == MODULE_STATUS_RUNNING) {
        ota_unlock();
        return 0;
    }
    g_ota.status = MODULE_STATUS_RUNNING;
    ota_unlock();

    LOG_INF("OTA module started");
    return 0;
}

int ota_module_stop(void) {
    LOG_INF("Stopping OTA module...");

    ota_lock();
    ota_reset_session_locked();
    g_ota.status = MODULE_STATUS_STOPPED;
    ota_unlock();

    LOG_INF("OTA module stopped");
    return 0;
}

int ota_module_shutdown(void) {
    (void) ota_module_stop();

    ota_lock();
    g_ota.status = MODULE_STATUS_UNINITIALIZED;
    ota_unlock();

    LOG_INF("OTA module shutdown");
    return 0;
}

void ota_module_on_event(const event_t* event, void* user_data) {
    ARG_UNUSED(event);
    ARG_UNUSED(user_data);
}

module_status_t ota_module_get_status(void) {
    module_status_t st;

    ota_lock();
    st = g_ota.status;
    ota_unlock();
    return st;
}

int ota_module_control(int cmd, void* arg) {
    ARG_UNUSED(cmd);
    ARG_UNUSED(arg);
    return -ENOTSUP;
}

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

int ota_module_begin_update(void) {
    int            ret;
    ota_progress_t prog;
    ota_state_t    state;

    ota_lock();

    if (g_ota.status == MODULE_STATUS_UNINITIALIZED) {
        ota_unlock();
        return APP_ERR_INIT;
    }

    if (g_ota.transport == NULL || g_ota.transport->open == NULL) {
        ota_unlock();
        return APP_ERR_OTA_TRANSPORT;
    }

    state = ota_sm_get_state(&g_ota.sm);
    if (state == OTA_STATE_ERROR || state == OTA_STATE_READY_REBOOT) {
        ota_reset_session_locked();
    }

    ret = ota_sm_on_download_start(&g_ota.sm);
    if (ret != 0) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }

    ret = g_ota.transport->open((ota_transport_ops_t*) g_ota.transport);
    if (ret != 0) {
        ret = ota_transport_fail_locked(ret, &prog);
        ota_unlock();
        (void) ota_publish_progress(&prog);
        return APP_ERR_OTA_TRANSPORT;
    }

    g_ota.transport_open = true;
    g_ota.image_size = 0U;
    ota_build_progress(&prog, OTA_STATE_DOWNLOADING, 0);
    ota_unlock();
    (void) ota_publish_progress(&prog);
    return 0;
}

int ota_module_write_chunk(size_t offset, const uint8_t* data, size_t len) {
    int            ret;
    ota_progress_t prog;

    if (data == NULL || len == 0U) {
        return APP_ERR_INVALID_PARAM;
    }

    ota_lock();

    if (ota_sm_get_state(&g_ota.sm) != OTA_STATE_DOWNLOADING || !g_ota.transport_open) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }

    ret = g_ota.transport->write_chunk((ota_transport_ops_t*) g_ota.transport, offset, data, len);
    if (ret != 0) {
        ret = ota_transport_fail_locked(ret, &prog);
        ota_unlock();
        (void) ota_publish_progress(&prog);
        return ret;
    }

    if (offset + len > g_ota.image_size) {
        g_ota.image_size = offset + len;
    }
    ota_build_progress(&prog, OTA_STATE_DOWNLOADING, 0);
    ota_unlock();
    (void) ota_publish_progress(&prog);
    return 0;
}

int ota_module_finish_update(void) {
    int            ret;
    ota_progress_t prog;

    ota_lock();

    if (ota_sm_get_state(&g_ota.sm) != OTA_STATE_DOWNLOADING || !g_ota.transport_open) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }

    ret = ota_sm_on_download_complete(&g_ota.sm);
    if (ret != 0) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }
    ota_build_progress(&prog, OTA_STATE_VERIFYING, 0);
    ota_unlock();
    (void) ota_publish_progress(&prog);

    ota_lock();
    /* 释放锁期间可能发生并发 abort/stop，重锁后须重新校验会话仍有效 */
    if (ota_sm_get_state(&g_ota.sm) != OTA_STATE_VERIFYING || !g_ota.transport_open) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }
    ret = g_ota.transport->verify((ota_transport_ops_t*) g_ota.transport);
    if (ret != 0) {
        ret = ota_transport_fail_locked(ret, &prog);
        ota_unlock();
        (void) ota_publish_progress(&prog);
        return ret;
    }

    ret = ota_sm_on_verify_ok(&g_ota.sm);
    if (ret != 0) {
        ota_unlock();
        return APP_ERR_OTA_INVALID_STATE;
    }

    if (g_ota.transport->close != NULL) {
        (void) g_ota.transport->close((ota_transport_ops_t*) g_ota.transport);
    }
    g_ota.transport_open = false;
    ota_build_progress(&prog, OTA_STATE_READY_REBOOT, 0);
    ota_unlock();
    (void) ota_publish_progress(&prog);
    return 0;
}

int ota_module_get_state(ota_state_t* out_state) {
    if (out_state == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    ota_lock();
    *out_state = ota_sm_get_state(&g_ota.sm);
    ota_unlock();
    return 0;
}

int ota_module_abort_update(void) {
    ota_progress_t prog;

    ota_lock();
    ota_reset_session_locked();
    ota_build_progress(&prog, OTA_STATE_IDLE, 0);
    ota_unlock();
    (void) ota_publish_progress(&prog);
    return 0;
}

int ota_module_request_reboot(void) {
    ota_state_t st;

    ota_lock();
    st = ota_sm_get_state(&g_ota.sm);
    ota_unlock();

    if (st != OTA_STATE_READY_REBOOT) {
        return APP_ERR_OTA_INVALID_STATE;
    }

    LOG_INF("OTA reboot requested");
    sys_reboot(SYS_REBOOT_WARM);
    return 0;
}

/* =============================================================================
 * 模块注册
 * ============================================================================= */

DECLARE_MODULE_INTERFACE(ota_module);

#if IS_ENABLED(CONFIG_OTA_MODULE_AUTOINIT)
static int ota_module_auto_register(void) {
    uint32_t id;

    return module_manager_register(&ota_module_interface, NULL, &id) ? -EIO : 0;
}

SYS_INIT(ota_module_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_OTA);
#endif
