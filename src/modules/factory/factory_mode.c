/**
 * @file factory_mode.c
 * @brief 工厂产测模式模块实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 4 初始版本
 *
 */

#include <zeplod/factory_mode.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/app_kv.h>
#include <zeplod/lock_order.h>
#include <zeplod/module_manager.h>

LOG_MODULE_REGISTER(factory_mode, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    char  key[CONFIG_FACTORY_MODE_CAL_KEY_MAX_LEN];
    char  value[CONFIG_FACTORY_MODE_CAL_VALUE_MAX_LEN];
    bool  used;
} factory_cal_slot_t;

typedef struct {
    factory_state_t    state;
    bool               gpio_passed;
    int                last_error;
    module_status_t    module_status;
    factory_cal_slot_t cal_slots[CONFIG_FACTORY_MODE_CAL_MAX_ENTRIES];
    struct k_mutex     lock;
    bool               lock_ready;
    bool               events_registered;
} factory_mode_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static factory_mode_cb_t g_factory;

/* =============================================================================
 * 前置声明
 * ============================================================================= */

static void factory_lock(void);
static void factory_unlock(void);
static int  factory_register_event_types(void);
static int  factory_publish_state(factory_state_t state, int err);
static void factory_clear_cal_slots_locked(void);
static int  factory_find_cal_slot_locked(const char* key);
static int  factory_alloc_cal_slot_locked(void);

/* =============================================================================
 * 锁与内部辅助
 * ============================================================================= */

static void factory_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_factory.lock);
    k_mutex_lock(&g_factory.lock, K_FOREVER);
}

static void factory_unlock(void) {
    k_mutex_unlock(&g_factory.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_factory.lock);
}

static int factory_register_event_types(void) {
    event_status_t st;

    if (g_factory.events_registered) {
        return 0;
    }

    st = event_register_type(EVENT_FACTORY_STATE_CHANGED, "factory_state");
    if (st != EVENT_OK) {
        LOG_ERR("register EVENT_FACTORY_STATE_CHANGED failed: %d", st);
        return -EIO;
    }

    g_factory.events_registered = true;
    return 0;
}

static int factory_publish_state(factory_state_t state, int err) {
    factory_status_t st = {.state = state, .error_code = err, .gpio_passed = g_factory.gpio_passed};
    event_status_t   ev_st;

    ev_st = event_publish_copy(EVENT_FACTORY_STATE_CHANGED, EVENT_PRIORITY_NORMAL, &st, sizeof(st));
    if (ev_st != EVENT_OK) {
        LOG_WRN("factory state event publish failed: %d", ev_st);
        return -EIO;
    }
    return 0;
}

static void factory_clear_cal_slots_locked(void) {
    for (size_t i = 0U; i < CONFIG_FACTORY_MODE_CAL_MAX_ENTRIES; i++) {
        g_factory.cal_slots[i].used = false;
        g_factory.cal_slots[i].key[0] = '\0';
        g_factory.cal_slots[i].value[0] = '\0';
    }
}

static int factory_find_cal_slot_locked(const char* key) {
    if (key == NULL) {
        return -1;
    }

    for (size_t i = 0U; i < CONFIG_FACTORY_MODE_CAL_MAX_ENTRIES; i++) {
        if (g_factory.cal_slots[i].used && strcmp(g_factory.cal_slots[i].key, key) == 0) {
            return (int) i;
        }
    }
    return -1;
}

static int factory_alloc_cal_slot_locked(void) {
    for (size_t i = 0U; i < CONFIG_FACTORY_MODE_CAL_MAX_ENTRIES; i++) {
        if (!g_factory.cal_slots[i].used) {
            return (int) i;
        }
    }
    return -1;
}

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

int factory_mode_enter(void) {
    factory_lock();

    if (g_factory.module_status == MODULE_STATUS_UNINITIALIZED) {
        factory_unlock();
        return APP_ERR_INIT;
    }
    if (g_factory.state != FACTORY_STATE_INACTIVE) {
        factory_unlock();
        return APP_ERR_FACTORY;
    }

    g_factory.state = FACTORY_STATE_ACTIVE;
    g_factory.gpio_passed = false;
    g_factory.last_error = 0;
    factory_clear_cal_slots_locked();
    factory_unlock();

    (void) factory_publish_state(FACTORY_STATE_ACTIVE, 0);
    LOG_INF("Factory mode entered");
    return 0;
}

int factory_mode_exit(void) {
    factory_lock();

    if (g_factory.state == FACTORY_STATE_INACTIVE) {
        factory_unlock();
        return 0;
    }

    g_factory.state = FACTORY_STATE_INACTIVE;
    g_factory.gpio_passed = false;
    g_factory.last_error = 0;
    factory_clear_cal_slots_locked();
    factory_unlock();

    (void) factory_publish_state(FACTORY_STATE_INACTIVE, 0);
    LOG_INF("Factory mode exited");
    return 0;
}

int factory_mode_get_state(factory_status_t* out) {
    if (out == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    factory_lock();
    out->state = g_factory.state;
    out->error_code = g_factory.last_error;
    out->gpio_passed = g_factory.gpio_passed;
    factory_unlock();
    return 0;
}

int factory_mode_run_gpio_loopback(void) {
    factory_lock();

    if (g_factory.state != FACTORY_STATE_ACTIVE) {
        factory_unlock();
        return APP_ERR_FACTORY;
    }

#if IS_ENABLED(CONFIG_FACTORY_MODE_GPIO_STUB)
    g_factory.gpio_passed = true;
    factory_unlock();
    LOG_INF("GPIO loopback stub passed");
    return 0;
#else
    factory_unlock();
    return -ENOTSUP;
#endif
}

int factory_mode_set_calibration(const char* key, const char* value) {
    int slot;
    size_t klen;
    size_t vlen;

    if (key == NULL || value == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    klen = strlen(key);
    vlen = strlen(value);
    if (klen == 0U || klen >= CONFIG_FACTORY_MODE_CAL_KEY_MAX_LEN) {
        return APP_ERR_INVALID_PARAM;
    }
    if (vlen >= CONFIG_FACTORY_MODE_CAL_VALUE_MAX_LEN) {
        return -ENOMEM;
    }

    factory_lock();

    if (g_factory.state != FACTORY_STATE_ACTIVE) {
        factory_unlock();
        return APP_ERR_FACTORY;
    }

    slot = factory_find_cal_slot_locked(key);
    if (slot < 0) {
        slot = factory_alloc_cal_slot_locked();
        if (slot < 0) {
            factory_unlock();
            return -ENOMEM;
        }
        (void) strncpy(g_factory.cal_slots[slot].key, key, sizeof(g_factory.cal_slots[slot].key) - 1U);
        g_factory.cal_slots[slot].key[sizeof(g_factory.cal_slots[slot].key) - 1U] = '\0';
        g_factory.cal_slots[slot].used = true;
    }

    (void) strncpy(g_factory.cal_slots[slot].value, value, sizeof(g_factory.cal_slots[slot].value) - 1U);
    g_factory.cal_slots[slot].value[sizeof(g_factory.cal_slots[slot].value) - 1U] = '\0';
    factory_unlock();
    return 0;
}

int factory_mode_get_calibration(const char* key, char* out, size_t out_len) {
    int slot;

    if (key == NULL || out == NULL || out_len == 0U) {
        return APP_ERR_INVALID_PARAM;
    }

    factory_lock();
    slot = factory_find_cal_slot_locked(key);
    if (slot < 0) {
        factory_unlock();
        return APP_ERR_NOT_FOUND;
    }

    if (strlen(g_factory.cal_slots[slot].value) >= out_len) {
        factory_unlock();
        return -ENOMEM;
    }

    (void) strncpy(out, g_factory.cal_slots[slot].value, out_len - 1U);
    out[out_len - 1U] = '\0';
    factory_unlock();
    return 0;
}

int factory_mode_finalize_pass(void) {
    char kv_key[CONFIG_FACTORY_MODE_CAL_KEY_MAX_LEN + 16U];
    int  ret = 0;

#if !IS_ENABLED(CONFIG_APP_KV_ENABLE)
    return APP_ERR_DISABLED;
#endif

    factory_lock();

    if (g_factory.state != FACTORY_STATE_ACTIVE) {
        factory_unlock();
        return APP_ERR_FACTORY;
    }
    if (!g_factory.gpio_passed) {
        factory_unlock();
        return APP_ERR_FACTORY;
    }

    for (size_t i = 0U; i < CONFIG_FACTORY_MODE_CAL_MAX_ENTRIES; i++) {
        if (!g_factory.cal_slots[i].used) {
            continue;
        }

        (void) snprintf(kv_key, sizeof(kv_key), "factory.cal.%s", g_factory.cal_slots[i].key);
        factory_unlock();

        ret = app_kv_set(kv_key, g_factory.cal_slots[i].value);
        if (ret != APP_OK) {
            factory_lock();
            g_factory.state = FACTORY_STATE_FAILED;
            g_factory.last_error = ret;
            factory_unlock();
            (void) factory_publish_state(FACTORY_STATE_FAILED, ret);
            return ret;
        }

        factory_lock();
    }

    g_factory.state = FACTORY_STATE_PASSED;
    g_factory.last_error = 0;
    factory_unlock();

    (void) factory_publish_state(FACTORY_STATE_PASSED, 0);
    LOG_INF("Factory pass finalized");
    return 0;
}

/* =============================================================================
 * 模块接口实现
 * ============================================================================= */

int factory_mode_init(void* config) {
    int ret;

    ARG_UNUSED(config);

    if (g_factory.module_status != MODULE_STATUS_UNINITIALIZED) {
        return 0;
    }

    if (!g_factory.lock_ready) {
        k_mutex_init(&g_factory.lock);
        g_factory.lock_ready = true;
    }

    g_factory.state = FACTORY_STATE_INACTIVE;
    g_factory.gpio_passed = false;
    g_factory.last_error = 0;
    factory_clear_cal_slots_locked();
    g_factory.module_status = MODULE_STATUS_INITIALIZED;

    ret = factory_register_event_types();
    if (ret != 0) {
        g_factory.module_status = MODULE_STATUS_UNINITIALIZED;
        return ret;
    }

    LOG_INF("Factory mode module initialized");
    return 0;
}

int factory_mode_start(void) {
    factory_lock();
    if (g_factory.module_status == MODULE_STATUS_UNINITIALIZED) {
        factory_unlock();
        return APP_ERR_INIT;
    }
    if (g_factory.module_status == MODULE_STATUS_RUNNING) {
        factory_unlock();
        return 0;
    }
    g_factory.module_status = MODULE_STATUS_RUNNING;
    factory_unlock();

    LOG_INF("Factory mode module started");
    return 0;
}

int factory_mode_stop(void) {
    factory_lock();
    if (g_factory.module_status == MODULE_STATUS_RUNNING) {
        g_factory.module_status = MODULE_STATUS_STOPPED;
    }
    factory_unlock();
    return 0;
}

int factory_mode_shutdown(void) {
    (void) factory_mode_exit();

    factory_lock();
    g_factory.module_status = MODULE_STATUS_UNINITIALIZED;
    factory_unlock();
    return 0;
}

void factory_mode_on_event(const event_t* event, void* user_data) {
    ARG_UNUSED(event);
    ARG_UNUSED(user_data);
}

module_status_t factory_mode_get_status(void) {
    module_status_t st;

    factory_lock();
    st = g_factory.module_status;
    factory_unlock();
    return st;
}

int factory_mode_control(int cmd, void* arg) {
    ARG_UNUSED(arg);

    if (cmd == FACTORY_MODE_CMD_RESET) {
        factory_lock();
        g_factory.state = FACTORY_STATE_INACTIVE;
        g_factory.gpio_passed = false;
        g_factory.last_error = 0;
        factory_clear_cal_slots_locked();
        factory_unlock();
        return 0;
    }
    return -ENOTSUP;
}

/* =============================================================================
 * 模块注册
 * ============================================================================= */

DECLARE_MODULE_INTERFACE(factory_mode);

#if IS_ENABLED(CONFIG_FACTORY_MODE_MODULE_AUTOINIT)
static int factory_mode_auto_register(void) {
    uint32_t id;

    return module_manager_register(&factory_mode_interface, NULL, &id) ? -EIO : 0;
}

SYS_INIT(factory_mode_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_FACTORY);
#endif
