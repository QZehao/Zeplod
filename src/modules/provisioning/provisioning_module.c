/**
 * @file provisioning_module.c
 * @brief 配网模块实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 3 初始版本
 *
 */

#include <zeplod/provisioning_module.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/lock_order.h>
#include <zeplod/module_manager.h>

LOG_MODULE_REGISTER(provisioning_module, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    provisioning_state_t state;
    module_status_t      module_status;
    struct k_mutex       lock;
    bool                 lock_ready;
    bool                 events_registered;
} provisioning_module_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static provisioning_module_cb_t g_prov;

/* =============================================================================
 * 前置声明
 * ============================================================================= */

static void prov_lock(void);
static void prov_unlock(void);
static int  prov_register_event_types(void);
static int  prov_publish_state(provisioning_state_t state, int err);

/* =============================================================================
 * 锁与内部辅助
 * ============================================================================= */

static void prov_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_prov.lock);
    k_mutex_lock(&g_prov.lock, K_FOREVER);
}

static void prov_unlock(void) {
    k_mutex_unlock(&g_prov.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_prov.lock);
}

static int prov_register_event_types(void) {
    event_status_t st;

    if (g_prov.events_registered) {
        return 0;
    }

    st = event_register_type(EVENT_PROVISIONING_STATE_CHANGED, "prov_state");
    if (st != EVENT_OK) {
        LOG_ERR("register EVENT_PROVISIONING_STATE_CHANGED failed: %d", st);
        return -EIO;
    }

    g_prov.events_registered = true;
    return 0;
}

static int prov_publish_state(provisioning_state_t state, int err) {
    provisioning_status_t st = {.state = state, .error_code = err};
    event_status_t        ev_st;

    ev_st = event_publish_copy(EVENT_PROVISIONING_STATE_CHANGED, EVENT_PRIORITY_NORMAL, &st, sizeof(st));
    if (ev_st != EVENT_OK) {
        LOG_WRN("provisioning state event publish failed: %d", ev_st);
        return -EIO;
    }
    return 0;
}

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

int provisioning_module_begin(const provisioning_credentials_t* creds) {
    ARG_UNUSED(creds);

    prov_lock();
    if (g_prov.module_status == MODULE_STATUS_UNINITIALIZED) {
        prov_unlock();
        return APP_ERR_INIT;
    }
    if (g_prov.module_status != MODULE_STATUS_RUNNING) {
        prov_unlock();
        return APP_ERR_INIT;
    }
    if (g_prov.state == PROVISIONING_STATE_PROVISIONED) {
        prov_unlock();
        return APP_ERR_PROVISIONING;
    }

    g_prov.state = PROVISIONING_STATE_IN_PROGRESS;
    prov_unlock();
    (void) prov_publish_state(PROVISIONING_STATE_IN_PROGRESS, 0);

    prov_lock();
    g_prov.state = PROVISIONING_STATE_PROVISIONED;
    prov_unlock();
    (void) prov_publish_state(PROVISIONING_STATE_PROVISIONED, 0);
    return 0;
}

int provisioning_module_reset(void) {
    prov_lock();
    g_prov.state = PROVISIONING_STATE_UNPROVISIONED;
    prov_unlock();
    (void) prov_publish_state(PROVISIONING_STATE_UNPROVISIONED, 0);
    return 0;
}

int provisioning_module_get_state(provisioning_state_t* out_state) {
    if (out_state == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    prov_lock();
    *out_state = g_prov.state;
    prov_unlock();
    return 0;
}

int provisioning_module_get_device_id(char* out, size_t out_len) {
    if (out == NULL || out_len == 0U) {
        return APP_ERR_INVALID_PARAM;
    }

    if (strlen(CONFIG_PROVISIONING_DEVICE_ID) >= out_len) {
        return -ENOMEM;
    }

    (void) strncpy(out, CONFIG_PROVISIONING_DEVICE_ID, out_len - 1U);
    out[out_len - 1U] = '\0';
    return 0;
}

/* =============================================================================
 * 模块接口实现
 * ============================================================================= */

int provisioning_module_init(void* config) {
    int ret;

    ARG_UNUSED(config);

    if (g_prov.module_status != MODULE_STATUS_UNINITIALIZED) {
        return 0;
    }

    if (!g_prov.lock_ready) {
        k_mutex_init(&g_prov.lock);
        g_prov.lock_ready = true;
    }

    g_prov.state = PROVISIONING_STATE_UNPROVISIONED;
    g_prov.module_status = MODULE_STATUS_INITIALIZED;

    ret = prov_register_event_types();
    if (ret != 0) {
        g_prov.module_status = MODULE_STATUS_UNINITIALIZED;
        return ret;
    }

    LOG_INF("Provisioning module initialized");
    return 0;
}

int provisioning_module_start(void) {
    prov_lock();
    if (g_prov.module_status == MODULE_STATUS_UNINITIALIZED) {
        prov_unlock();
        return APP_ERR_INIT;
    }
    if (g_prov.module_status == MODULE_STATUS_RUNNING) {
        prov_unlock();
        return 0;
    }
    g_prov.module_status = MODULE_STATUS_RUNNING;
    prov_unlock();

    LOG_INF("Provisioning module started");
    return 0;
}

int provisioning_module_stop(void) {
    prov_lock();
    if (g_prov.module_status == MODULE_STATUS_RUNNING) {
        g_prov.module_status = MODULE_STATUS_STOPPED;
    }
    prov_unlock();
    return 0;
}

int provisioning_module_shutdown(void) {
    (void) provisioning_module_stop();

    prov_lock();
    g_prov.module_status = MODULE_STATUS_UNINITIALIZED;
    prov_unlock();
    return 0;
}

void provisioning_module_on_event(const event_t* event, void* user_data) {
    ARG_UNUSED(event);
    ARG_UNUSED(user_data);
}

module_status_t provisioning_module_get_status(void) {
    module_status_t st;

    prov_lock();
    st = g_prov.module_status;
    prov_unlock();
    return st;
}

int provisioning_module_control(int cmd, void* arg) {
    ARG_UNUSED(cmd);
    ARG_UNUSED(arg);
    return -ENOTSUP;
}

/* =============================================================================
 * 模块注册
 * ============================================================================= */

DECLARE_MODULE_INTERFACE(provisioning_module);

#if IS_ENABLED(CONFIG_PROVISIONING_MODULE_AUTOINIT)
static int provisioning_module_auto_register(void) {
    uint32_t id;

    return module_manager_register(&provisioning_module_interface, NULL, &id) ? -EIO : 0;
}

SYS_INIT(provisioning_module_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_PROVISIONING);
#endif
