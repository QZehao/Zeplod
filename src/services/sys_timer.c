/**
 * @file sys_timer.c
 * @brief 系统定时器服务实现
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

#include "sys_timer.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(sys_timer, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#ifndef CONFIG_SYS_TIMER_STACK_SIZE
#define CONFIG_SYS_TIMER_STACK_SIZE 1024
#endif

#ifndef CONFIG_SYS_TIMER_PRIORITY
#define CONFIG_SYS_TIMER_PRIORITY 5
#endif

#define MAX_TIMERS  32
#define TIMER_MAGIC 0x544D5253 /* "TMRS" */

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

struct sys_timer {
    uint32_t           magic;
    uint32_t           index;
    bool               thread_started;
    sys_timer_config_t config;
    sys_timer_status_t status;
    struct k_thread    thread;
    K_KERNEL_STACK_MEMBER(stack, CONFIG_SYS_TIMER_STACK_SIZE);
    struct k_sem sem;
    uint32_t     fire_count;
    uint32_t     last_fire_time;
    uint32_t     next_fire_time;
    uint32_t     avg_latency_us;
    uint32_t     max_latency_us;
    bool         is_allocated;
};

typedef struct {
    struct sys_timer timers[MAX_TIMERS];
    uint32_t         timer_count;
    struct k_mutex   lock;
    bool             initialized;
} sys_timer_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static sys_timer_cb_t g_sys_timer;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void timer_thread_func(void* p1, void* p2, void* p3);

/* =============================================================================
 * Core API Implementation
 * ============================================================================= */

int sys_timer_init(void) {
    LOG_INF("Initializing timer system...");

    memset(&g_sys_timer, 0, sizeof(g_sys_timer));
    k_mutex_init(&g_sys_timer.lock);

    for (int i = 0; i < MAX_TIMERS; i++) {
        g_sys_timer.timers[i].magic = TIMER_MAGIC;
        g_sys_timer.timers[i].is_allocated = false;
        k_sem_init(&g_sys_timer.timers[i].sem, 0, 1);
    }

    g_sys_timer.initialized = true;
    LOG_INF("Timer system initialized");
    return 0;
}

sys_timer_handle_t sys_timer_create(const sys_timer_config_t* config) {
    if (!g_sys_timer.initialized || config == NULL) {
        return NULL;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    /* Find free timer slot */
    sys_timer_handle_t timer = NULL;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_sys_timer.timers[i].is_allocated) {
            timer = &g_sys_timer.timers[i];
            timer->index = i;
            break;
        }
    }

    if (timer == NULL) {
        k_mutex_unlock(&g_sys_timer.lock);
        LOG_ERR("No free timer slots");
        return NULL;
    }

    /* Initialize timer */
    timer->config = *config;
    timer->status = SYS_TIMER_STOPPED;
    timer->fire_count = 0;
    timer->last_fire_time = 0;
    timer->next_fire_time = 0;
    timer->avg_latency_us = 0;
    timer->max_latency_us = 0;
    timer->is_allocated = true;
    timer->thread_started = false;

    k_sem_init(&timer->sem, 0, 1);

    /* Create timer thread */
    int priority = config->priority != 0 ? config->priority : CONFIG_SYS_TIMER_PRIORITY;

    k_thread_create(&timer->thread, timer->stack, K_THREAD_STACK_SIZEOF(timer->stack), timer_thread_func, timer, NULL,
                    NULL, priority, 0, K_FOREVER);

    char name[16];
    snprintf(name, sizeof(name), "timer_%s", config->name != NULL ? config->name : "unk");
    k_thread_name_set(&timer->thread, name);

    g_sys_timer.timer_count++;

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer created: %s", config->name != NULL ? config->name : "unnamed");
    return timer;
}

int sys_timer_delete(sys_timer_handle_t timer) {
    if (timer == NULL || !g_sys_timer.initialized) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (!timer->is_allocated || timer->magic != TIMER_MAGIC) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    /* Stop timer if running */
    if (timer->status == SYS_TIMER_RUNNING || timer->status == SYS_TIMER_PAUSED) {
        timer->status = SYS_TIMER_STOPPED;
        k_sem_give(&timer->sem); /* Wake up thread */
        k_thread_abort(&timer->thread);
    }

    /* Clear timer */
    timer->is_allocated = false;
    timer->thread_started = false;
    timer->config.callback = NULL;
    timer->config.user_data = NULL;
    g_sys_timer.timer_count--;

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer deleted");
    return 0;
}

int sys_timer_start(sys_timer_handle_t timer) {
    if (timer == NULL || !g_sys_timer.initialized) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (!timer->is_allocated || timer->magic != TIMER_MAGIC) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    if (timer->status == SYS_TIMER_RUNNING) {
        k_mutex_unlock(&g_sys_timer.lock);
        return 0;
    }

    timer->status = SYS_TIMER_RUNNING;
    timer->next_fire_time = k_uptime_get_32() + timer->config.delay_ms;

    k_sem_give(&timer->sem); /* Wake up thread */

    if (!timer->thread_started) {
        k_thread_start(&timer->thread);
        timer->thread_started = true;
    }

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer started: %s", timer->config.name != NULL ? timer->config.name : "unnamed");
    return 0;
}

int sys_timer_stop(sys_timer_handle_t timer) {
    if (timer == NULL) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (!timer->is_allocated) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    timer->status = SYS_TIMER_STOPPED;
    k_sem_give(&timer->sem); /* Wake up thread to check status */

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer stopped");
    return 0;
}

int sys_timer_restart(sys_timer_handle_t timer) {
    sys_timer_stop(timer);
    k_msleep(10); /* Give thread time to stop */
    return sys_timer_start(timer);
}

int sys_timer_pause(sys_timer_handle_t timer) {
    if (timer == NULL) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (timer->status != SYS_TIMER_RUNNING) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    timer->status = SYS_TIMER_PAUSED;

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer paused");
    return 0;
}

int sys_timer_resume(sys_timer_handle_t timer) {
    if (timer == NULL) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (timer->status != SYS_TIMER_PAUSED) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    timer->status = SYS_TIMER_RUNNING;
    timer->next_fire_time = k_uptime_get_32() + timer->config.delay_ms;

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer resumed");
    return 0;
}

sys_timer_status_t sys_timer_get_status(sys_timer_handle_t timer) {
    if (timer == NULL) {
        return SYS_TIMER_STOPPED;
    }
    return timer->status;
}

int sys_timer_set_period(sys_timer_handle_t timer, uint32_t period_ms) {
    if (timer == NULL || period_ms == 0) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (!timer->is_allocated) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    timer->config.period_ms = period_ms;

    k_mutex_unlock(&g_sys_timer.lock);

    LOG_DBG("Timer period set to %dms", period_ms);
    return 0;
}

uint32_t sys_timer_get_time_until_expiry(sys_timer_handle_t timer) {
    if (timer == NULL || timer->status != SYS_TIMER_RUNNING) {
        return 0;
    }

    uint32_t now = k_uptime_get_32();
    if (timer->next_fire_time <= now) {
        return 0;
    }

    return timer->next_fire_time - now;
}

/* =============================================================================
 * Statistics API
 * ============================================================================= */

int sys_timer_get_stats(sys_timer_handle_t timer, sys_timer_stats_t* stats) {
    if (timer == NULL || stats == NULL) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (!timer->is_allocated) {
        k_mutex_unlock(&g_sys_timer.lock);
        return -1;
    }

    stats->fire_count = timer->fire_count;
    stats->miss_count = 0; /* Could be implemented */
    stats->last_fire_time_ms = timer->last_fire_time;
    stats->avg_latency_us = timer->avg_latency_us;
    stats->max_latency_us = timer->max_latency_us;

    k_mutex_unlock(&g_sys_timer.lock);
    return 0;
}

int sys_timer_reset_stats(sys_timer_handle_t timer) {
    if (timer == NULL) {
        return -1;
    }

    k_mutex_lock(&g_sys_timer.lock, K_FOREVER);

    if (timer->is_allocated) {
        timer->fire_count = 0;
        timer->last_fire_time = 0;
        timer->avg_latency_us = 0;
        timer->max_latency_us = 0;
    }

    k_mutex_unlock(&g_sys_timer.lock);
    return 0;
}

/* =============================================================================
 * Convenience Functions
 * ============================================================================= */

sys_timer_handle_t sys_timer_oneshot(uint32_t delay_ms, sys_timer_callback_t callback, void* user_data) {
    sys_timer_config_t config = {.mode = SYS_TIMER_ONESHOT,
                                 .delay_ms = delay_ms,
                                 .period_ms = 0,
                                 .callback = callback,
                                 .user_data = user_data,
                                 .name = "oneshot",
                                 .priority = 0};

    sys_timer_handle_t timer = sys_timer_create(&config);
    if (timer != NULL) {
        sys_timer_start(timer);
    }
    return timer;
}

sys_timer_handle_t sys_timer_periodic(uint32_t period_ms, sys_timer_callback_t callback, void* user_data) {
    sys_timer_config_t config = {.mode = SYS_TIMER_PERIODIC,
                                 .delay_ms = period_ms,
                                 .period_ms = period_ms,
                                 .callback = callback,
                                 .user_data = user_data,
                                 .name = "periodic",
                                 .priority = 0};

    sys_timer_handle_t timer = sys_timer_create(&config);
    if (timer != NULL) {
        sys_timer_start(timer);
    }
    return timer;
}

void sys_timer_sleep(uint32_t ms) {
    k_msleep(ms);
}

uint32_t sys_timer_get_uptime(void) {
    return k_uptime_get_32();
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void timer_thread_func(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    sys_timer_handle_t timer = (sys_timer_handle_t) p1;

    LOG_DBG("Timer thread started: %s", timer->config.name != NULL ? timer->config.name : "unnamed");

    while (timer->status == SYS_TIMER_RUNNING) {
        /* Wait for trigger or timeout */
        uint32_t wait_time = timer->next_fire_time - k_uptime_get_32();

        if (wait_time > 0) {
            k_sem_take(&timer->sem, K_MSEC(wait_time));
        }

        /* Check status again */
        if (timer->status != SYS_TIMER_RUNNING) {
            break;
        }

        /* Check if it's time to fire */
        uint32_t now = k_uptime_get_32();
        if (now >= timer->next_fire_time) {
            /* Calculate latency */
            uint32_t latency_us = 0; /* Could measure actual latency */

            /* Call callback */
            if (timer->config.callback != NULL) {
                timer->config.callback(timer, timer->config.user_data);
            }

            /* Update statistics */
            timer->fire_count++;
            timer->last_fire_time = now;

            if (latency_us > timer->max_latency_us) {
                timer->max_latency_us = latency_us;
            }

            /* Calculate running average latency */
            timer->avg_latency_us = (timer->avg_latency_us * (timer->fire_count - 1) + latency_us) / timer->fire_count;

            /* Calculate next fire time */
            if (timer->config.mode == SYS_TIMER_PERIODIC) {
                timer->next_fire_time = now + timer->config.period_ms;
            } else {
                timer->status = SYS_TIMER_EXPIRED;
                break;
            }
        }
    }

    LOG_DBG("Timer thread stopped");
}
