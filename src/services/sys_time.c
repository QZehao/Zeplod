/**
 * @file sys_time.c
 * @brief 墙钟时间服务实现
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 3 初始版本
 * 2026-06-13       1.1            zeh            64 位 uptime、互斥锁
 *
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>

#include <zeplod/app_config.h>
#include <zeplod/lock_order.h>
#include <zeplod/sys_time.h>

LOG_MODULE_REGISTER(sys_time, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    int64_t      unix_ms_base;
    int64_t      uptime_ms_base;
    bool         valid;
    bool         ready;
    struct k_mutex lock;
    bool         lock_ready;
} sys_time_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static sys_time_cb_t g_sys_time;

/* =============================================================================
 * 锁辅助
 * ============================================================================= */

static void sys_time_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_sys_time.lock);
    k_mutex_lock(&g_sys_time.lock, K_FOREVER);
}

static void sys_time_unlock(void) {
    k_mutex_unlock(&g_sys_time.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_sys_time.lock);
}

/* =============================================================================
 * 核心 API
 * ============================================================================= */

int sys_time_init(void) {
    if (g_sys_time.ready) {
        return 0;
    }

    if (!g_sys_time.lock_ready) {
        k_mutex_init(&g_sys_time.lock);
        g_sys_time.lock_ready = true;
    }

    g_sys_time.unix_ms_base = 0;
    g_sys_time.uptime_ms_base = 0;
    g_sys_time.valid = false;
    g_sys_time.ready = true;
    LOG_INF("sys_time ready");
    return 0;
}

int sys_time_set_unix_ms(int64_t unix_ms) {
    if (!g_sys_time.ready) {
        return APP_ERR_INIT;
    }
    if (unix_ms < 0) {
        return APP_ERR_INVALID_PARAM;
    }

    sys_time_lock();
    g_sys_time.unix_ms_base = unix_ms;
    g_sys_time.uptime_ms_base = k_uptime_get();
    g_sys_time.valid = true;
    sys_time_unlock();
    return 0;
}

int sys_time_get_unix_ms(int64_t* out_unix_ms) {
    int64_t now_uptime;
    int64_t elapsed;

    if (!g_sys_time.ready) {
        return APP_ERR_INIT;
    }
    if (out_unix_ms == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    sys_time_lock();
    if (!g_sys_time.valid) {
        sys_time_unlock();
        return APP_ERR_TIME;
    }

    now_uptime = k_uptime_get();
    elapsed = now_uptime - g_sys_time.uptime_ms_base;
    *out_unix_ms = g_sys_time.unix_ms_base + elapsed;
    sys_time_unlock();
    return 0;
}

bool sys_time_is_valid(void) {
    bool valid;

    if (!g_sys_time.ready) {
        return false;
    }

    sys_time_lock();
    valid = g_sys_time.valid;
    sys_time_unlock();
    return valid;
}

void sys_time_invalidate(void) {
    if (!g_sys_time.ready) {
        return;
    }

    sys_time_lock();
    g_sys_time.valid = false;
    sys_time_unlock();
}

/* =============================================================================
 * SYS_INIT
 * ============================================================================= */

#if IS_ENABLED(CONFIG_SYS_TIME_ENABLE)

static int sys_time_auto_init(void) {
    return sys_time_init();
}

SYS_INIT(sys_time_auto_init, POST_KERNEL, APP_INIT_PRIO_SYS_TIME);

#endif
