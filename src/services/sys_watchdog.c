/**
 * @file sys_watchdog.c
 * @brief 系统看门狗服务实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include "sys_watchdog.h"
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(sys_watchdog, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#ifndef CONFIG_SYS_WATCHDOG_TIMEOUT_MS
#define CONFIG_SYS_WATCHDOG_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_MAX_MODULES
#define CONFIG_MAX_MODULES 16
#endif

#define MAX_MONITORED_THREADS CONFIG_MAX_MODULES
#define WDT_FEED_MARGIN_MS    1000 /* Feed at least 1 second before expiry */

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    k_tid_t  thread_id;
    char     name[32];
    uint32_t max_idle_ms;
    uint32_t last_alive_time;
    bool     is_monitored;
} thread_monitor_t;

typedef struct {
    wdt_status_t    status;
    wdt_config_t    config;
    wdt_stats_t     stats;
    struct k_thread monitor_thread;
    K_KERNEL_STACK_MEMBER(monitor_stack, 2048);
    struct k_mutex   lock;
    struct k_sem     feed_sem;
    thread_monitor_t threads[MAX_MONITORED_THREADS];
    uint32_t         thread_count;
    uint32_t         start_time;
    uint32_t         last_feed_time;
#ifdef CONFIG_WDT
    const struct device* wdt_dev;
    int                  wdt_channel;
#endif
} wdt_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static wdt_cb_t g_wdt;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void wdt_monitor_thread(void* p1, void* p2, void* p3);
static void wdt_feed_internal(void);
static void wdt_expire_handler(void);

/* =============================================================================
 * Core API Implementation
 * ============================================================================= */

int sys_wdt_init(const wdt_config_t* config) {
    LOG_INF("Initializing watchdog...");

    memset(&g_wdt, 0, sizeof(g_wdt));

    /* Set default or provided config */
    if (config != NULL) {
        g_wdt.config = *config;
    } else {
        g_wdt.config.mode = WDT_MODE_SOFTWARE;
        g_wdt.config.timeout_ms = CONFIG_SYS_WATCHDOG_TIMEOUT_MS;
        g_wdt.config.feed_margin_ms = WDT_FEED_MARGIN_MS;
        g_wdt.config.reset_on_expire = false;
        g_wdt.config.name = "sys_wdt";
    }

    /* Initialize synchronization primitives */
    k_mutex_init(&g_wdt.lock);
    k_sem_init(&g_wdt.feed_sem, 0, 1);

    g_wdt.status = WDT_STATUS_STOPPED;
    g_wdt.start_time = k_uptime_get_32();
    g_wdt.last_feed_time = g_wdt.start_time;

    /* Initialize hardware watchdog if configured */
#ifdef CONFIG_WDT
    if (g_wdt.config.mode == WDT_MODE_HARDWARE || g_wdt.config.mode == WDT_MODE_DUAL) {
        g_wdt.wdt_dev = device_get_binding("WDG"); /* Adjust device name */
        if (g_wdt.wdt_dev != NULL) {
            LOG_INF("Hardware watchdog device found");
            /* Hardware watchdog initialization would go here */
        } else {
            LOG_WRN("Hardware watchdog device not found, using software mode");
            g_wdt.config.mode = WDT_MODE_SOFTWARE;
        }
    }
#endif

    LOG_INF("Watchdog initialized: timeout=%dms, mode=%d", g_wdt.config.timeout_ms, g_wdt.config.mode);
    return 0;
}

int sys_wdt_start(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.status == WDT_STATUS_RUNNING) {
        k_mutex_unlock(&g_wdt.lock);
        return 0;
    }

    g_wdt.status = WDT_STATUS_RUNNING;
    g_wdt.last_feed_time = k_uptime_get_32();

    /* Create monitor thread */
    k_thread_create(&g_wdt.monitor_thread, g_wdt.monitor_stack, K_THREAD_STACK_SIZEOF(g_wdt.monitor_stack),
                    wdt_monitor_thread, NULL, NULL, NULL, 5, /* Priority */
                    0, K_FOREVER);

    k_thread_name_set(&g_wdt.monitor_thread, "wdt_mon");
    k_thread_start(&g_wdt.monitor_thread);

    k_mutex_unlock(&g_wdt.lock);

    LOG_INF("Watchdog started");
    return 0;
}

int sys_wdt_stop(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.status == WDT_STATUS_STOPPED) {
        k_mutex_unlock(&g_wdt.lock);
        return 0;
    }

    g_wdt.status = WDT_STATUS_STOPPED;
    k_mutex_unlock(&g_wdt.lock);

    /* Stop monitor thread */
    k_thread_abort(&g_wdt.monitor_thread);

    LOG_INF("Watchdog stopped");
    return 0;
}

int sys_wdt_feed(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.status != WDT_STATUS_RUNNING && g_wdt.status != WDT_STATUS_PAUSED) {
        k_mutex_unlock(&g_wdt.lock);
        return -1;
    }

    wdt_feed_internal();

    k_mutex_unlock(&g_wdt.lock);
    return 0;
}

int sys_wdt_pause(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.status != WDT_STATUS_RUNNING) {
        k_mutex_unlock(&g_wdt.lock);
        return -1;
    }

    g_wdt.status = WDT_STATUS_PAUSED;
    LOG_WRN("Watchdog paused");

    k_mutex_unlock(&g_wdt.lock);
    return 0;
}

int sys_wdt_resume(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.status != WDT_STATUS_PAUSED) {
        k_mutex_unlock(&g_wdt.lock);
        return -1;
    }

    g_wdt.status = WDT_STATUS_RUNNING;
    g_wdt.last_feed_time = k_uptime_get_32();
    LOG_INF("Watchdog resumed");

    k_mutex_unlock(&g_wdt.lock);
    return 0;
}

wdt_status_t sys_wdt_get_status(void) {
    return g_wdt.status;
}

/* =============================================================================
 * Thread Monitoring API
 * ============================================================================= */

int sys_wdt_monitor_thread(k_tid_t thread_id, const char* thread_name, uint32_t max_idle_ms) {
    if (thread_id == NULL || max_idle_ms == 0) {
        return -1;
    }

    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    if (g_wdt.thread_count >= MAX_MONITORED_THREADS) {
        k_mutex_unlock(&g_wdt.lock);
        LOG_ERR("Maximum monitored threads reached");
        return -1;
    }

    /* Find empty slot or existing thread */
    for (uint32_t i = 0; i < MAX_MONITORED_THREADS; i++) {
        if (!g_wdt.threads[i].is_monitored) {
            g_wdt.threads[i].thread_id = thread_id;
            strncpy(g_wdt.threads[i].name, thread_name != NULL ? thread_name : "unknown", 31);
            g_wdt.threads[i].max_idle_ms = max_idle_ms;
            g_wdt.threads[i].last_alive_time = k_uptime_get_32();
            g_wdt.threads[i].is_monitored = true;
            g_wdt.thread_count++;

            k_mutex_unlock(&g_wdt.lock);
            LOG_INF("Monitoring thread: %s (max_idle=%dms)", g_wdt.threads[i].name, max_idle_ms);
            return 0;
        }
    }

    k_mutex_unlock(&g_wdt.lock);
    return -1;
}

int sys_wdt_unmonitor_thread(k_tid_t thread_id) {
    if (thread_id == NULL) {
        return -1;
    }

    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    for (uint32_t i = 0; i < MAX_MONITORED_THREADS; i++) {
        if (g_wdt.threads[i].is_monitored && g_wdt.threads[i].thread_id == thread_id) {
            g_wdt.threads[i].is_monitored = false;
            g_wdt.thread_count--;

            k_mutex_unlock(&g_wdt.lock);
            LOG_INF("Stopped monitoring thread: %s", g_wdt.threads[i].name);
            return 0;
        }
    }

    k_mutex_unlock(&g_wdt.lock);
    return -1;
}

int sys_wdt_thread_alive(k_tid_t thread_id) {
    if (thread_id == NULL) {
        return -1;
    }

    k_mutex_lock(&g_wdt.lock, K_FOREVER);

    for (uint32_t i = 0; i < MAX_MONITORED_THREADS; i++) {
        if (g_wdt.threads[i].is_monitored && g_wdt.threads[i].thread_id == thread_id) {
            g_wdt.threads[i].last_alive_time = k_uptime_get_32();
            k_mutex_unlock(&g_wdt.lock);
            return 0;
        }
    }

    k_mutex_unlock(&g_wdt.lock);
    return -1; /* Thread not being monitored */
}

/* =============================================================================
 * Statistics & Debug API
 * ============================================================================= */

void sys_wdt_get_stats(wdt_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    k_mutex_lock(&g_wdt.lock, K_FOREVER);
    *stats = g_wdt.stats;
    k_mutex_unlock(&g_wdt.lock);
}

void sys_wdt_reset_stats(void) {
    k_mutex_lock(&g_wdt.lock, K_FOREVER);
    memset(&g_wdt.stats, 0, sizeof(g_wdt.stats));
    k_mutex_unlock(&g_wdt.lock);
}

uint32_t sys_wdt_get_time_since_feed(void) {
    uint32_t now = k_uptime_get_32();
    return now - g_wdt.last_feed_time;
}

uint32_t sys_wdt_get_time_until_expire(void) {
    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - g_wdt.last_feed_time;

    if (elapsed >= g_wdt.config.timeout_ms) {
        return 0;
    }

    return g_wdt.config.timeout_ms - elapsed;
}

void sys_wdt_simulate_expire(void) {
    LOG_WRN("Simulating watchdog expiration");
    wdt_expire_handler();
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void wdt_monitor_thread(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Watchdog monitor thread started");

    while (g_wdt.status == WDT_STATUS_RUNNING) {
        uint32_t now = k_uptime_get_32();
        uint32_t time_since_feed = now - g_wdt.last_feed_time;

        /* Check if watchdog needs feeding */
        if (time_since_feed >= g_wdt.config.timeout_ms - g_wdt.config.feed_margin_ms) {
            LOG_WRN("Watchdog timeout warning: %dms since last feed", time_since_feed);

            k_mutex_lock(&g_wdt.lock, K_FOREVER);
            g_wdt.stats.warning_count++;
            k_mutex_unlock(&g_wdt.lock);

            /* Call pre-expire callback if set */
            if (g_wdt.config.pre_expire_callback != NULL) {
                g_wdt.config.pre_expire_callback(g_wdt.config.callback_user_data);
            }
        }

        /* Check monitored threads */
        for (uint32_t i = 0; i < MAX_MONITORED_THREADS; i++) {
            if (!g_wdt.threads[i].is_monitored) {
                continue;
            }

            uint32_t idle_time = now - g_wdt.threads[i].last_alive_time;
            if (idle_time > g_wdt.threads[i].max_idle_ms) {
                LOG_ERR("Thread '%s' appears stuck: idle for %dms (max: %dms)", g_wdt.threads[i].name, idle_time,
                        g_wdt.threads[i].max_idle_ms);

                k_mutex_lock(&g_wdt.lock, K_FOREVER);
                g_wdt.stats.expire_count++;
                k_mutex_unlock(&g_wdt.lock);
            }
        }

        /* Feed the watchdog periodically */
        if (time_since_feed >= g_wdt.config.timeout_ms / 2) {
            wdt_feed_internal();
        }

        /* Sleep for a short period */
        k_msleep(100);
    }

    LOG_INF("Watchdog monitor thread stopped");
}

static void wdt_feed_internal(void) {
    uint32_t now = k_uptime_get_32();
    uint32_t interval = now - g_wdt.last_feed_time;

    g_wdt.last_feed_time = now;
    g_wdt.stats.feed_count++;

    if (interval > g_wdt.stats.max_feed_interval_ms) {
        g_wdt.stats.max_feed_interval_ms = interval;
    }

    g_wdt.stats.last_feed_time_ms = now;

#ifdef CONFIG_WDT
    if (g_wdt.wdt_dev != NULL && g_wdt.wdt_channel >= 0) {
        /* Feed hardware watchdog */
        wdt_feed(g_wdt.wdt_dev, g_wdt.wdt_channel);
    }
#endif

    LOG_DBG("Watchdog fed (interval: %dms)", interval);
}

static void wdt_expire_handler(void) {
    LOG_ERR("Watchdog expired!");

    k_mutex_lock(&g_wdt.lock, K_FOREVER);
    g_wdt.stats.expire_count++;
    g_wdt.status = WDT_STATUS_EXPIRED;
    k_mutex_unlock(&g_wdt.lock);

    if (g_wdt.config.reset_on_expire) {
        LOG_ERR("System reset triggered by watchdog");
        /* In real implementation: sys_reboot(SYS_REBOOT_COLD_START) */
    }
}
