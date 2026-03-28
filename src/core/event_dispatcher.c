/**
 * @file event_dispatcher.c
 * @brief Event Dispatcher Implementation
 * 
 * High-performance event dispatcher with priority scheduling and statistics.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "event_dispatcher.h"
#include "event_queue.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(event_dispatcher, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#define DEFAULT_STACK_SIZE        CONFIG_EVENT_DISPATCHER_STACK_SIZE
#define DEFAULT_PRIORITY          CONFIG_EVENT_DISPATCHER_PRIORITY
#define DEFAULT_MAX_EVENTS_CYCLE  100

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    dispatcher_state_t state;
    dispatcher_config_t config;
    dispatcher_stats_t stats;
    struct k_thread thread;
    K_THREAD_STACK_MEMBER(stack, DEFAULT_STACK_SIZE);
    event_filter_t filter;
    void *filter_user_data;
    struct k_mutex lock;
    struct k_sem sem;
    uint32_t events_in_batch;
    uint64_t last_event_time;
} dispatcher_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static dispatcher_cb_t g_dispatcher;
static struct k_msgq *g_event_queue;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void dispatcher_thread_func(void *p1, void *p2, void *p3);
static void process_event(const event_t *event);
static uint32_t calculate_latency_us(uint64_t event_timestamp);

/* =============================================================================
 * Dispatcher Control API
 * ============================================================================= */

event_status_t event_dispatcher_init(const dispatcher_config_t *config)
{
    LOG_INF("Initializing event dispatcher...");

    memset(&g_dispatcher, 0, sizeof(g_dispatcher));

    /* Set default or provided config */
    if (config != NULL) {
        g_dispatcher.config = *config;
    } else {
        g_dispatcher.config.stack_size = DEFAULT_STACK_SIZE;
        g_dispatcher.config.priority = DEFAULT_PRIORITY;
        g_dispatcher.config.thread_name = "event_disp";
        g_dispatcher.config.enable_stats = true;
        g_dispatcher.config.max_events_per_cycle = DEFAULT_MAX_EVENTS_CYCLE;
    }

    /* Initialize synchronization primitives */
    k_mutex_init(&g_dispatcher.lock);
    k_sem_init(&g_dispatcher.sem, 0, 1);

    g_dispatcher.state = DISPATCHER_STOPPED;
    g_dispatcher.last_event_time = k_uptime_get();

    /* Get reference to global event queue */
    extern struct k_msgq *event_system_get_queue(void);
    g_event_queue = event_system_get_queue();

    LOG_INF("Event dispatcher initialized");
    return EVENT_OK;
}

event_status_t event_dispatcher_start(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state == DISPATCHER_RUNNING) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_OK;
    }

    g_dispatcher.state = DISPATCHER_RUNNING;

    /* Create dispatcher thread */
    k_thread_create(&g_dispatcher.thread,
                    g_dispatcher.stack,
                    g_dispatcher.config.stack_size,
                    dispatcher_thread_func,
                    NULL, NULL, NULL,
                    g_dispatcher.config.priority,
                    0,
                    K_FOREVER);

    k_thread_name_set(&g_dispatcher.thread, 
                      g_dispatcher.config.thread_name);
    k_thread_start(&g_dispatcher.thread);

    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher started");
    return EVENT_OK;
}

event_status_t event_dispatcher_stop(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state == DISPATCHER_STOPPED) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_OK;
    }

    g_dispatcher.state = DISPATCHER_STOPPED;
    k_mutex_unlock(&g_dispatcher.lock);

    /* Wake up thread if waiting */
    k_sem_give(&g_dispatcher.sem);

    /* Wait for thread to stop */
    k_thread_abort(&g_dispatcher.thread);

    LOG_INF("Event dispatcher stopped");
    return EVENT_OK;
}

event_status_t event_dispatcher_pause(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state != DISPATCHER_RUNNING) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_ERR_INVALID_ARG;
    }

    g_dispatcher.state = DISPATCHER_PAUSED;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher paused");
    return EVENT_OK;
}

event_status_t event_dispatcher_resume(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state != DISPATCHER_PAUSED) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_ERR_INVALID_ARG;
    }

    g_dispatcher.state = DISPATCHER_RUNNING;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher resumed");
    return EVENT_OK;
}

dispatcher_state_t event_dispatcher_get_state(void)
{
    return g_dispatcher.state;
}

/* =============================================================================
 * Event Processing API
 * ============================================================================= */

void event_dispatcher_set_filter(event_filter_t filter, void *user_data)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.filter = filter;
    g_dispatcher.filter_user_data = user_data;
    k_mutex_unlock(&g_dispatcher.lock);
}

void event_dispatcher_clear_filter(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.filter = NULL;
    g_dispatcher.filter_user_data = NULL;
    k_mutex_unlock(&g_dispatcher.lock);
}

event_status_t event_dispatcher_process_one(k_timeout_t timeout)
{
    if (g_dispatcher.state != DISPATCHER_RUNNING &&
        g_dispatcher.state != DISPATCHER_PAUSED) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_t event;
    int ret = k_msgq_get(g_event_queue, &event, timeout);
    if (ret != 0) {
        return EVENT_ERR_QUEUE_EMPTY;
    }

    /* Apply filter if set */
    if (g_dispatcher.filter != NULL) {
        if (!g_dispatcher.filter(&event, g_dispatcher.filter_user_data)) {
            return EVENT_OK;  /* Filtered out, but not an error */
        }
    }

    process_event(&event);

    /* Free dynamic data */
    if (event.is_dynamic && event.data != NULL) {
        k_free(event.data);
    }

    return EVENT_OK;
}

uint32_t event_dispatcher_process_all(uint32_t max_events)
{
    if (max_events == 0) {
        max_events = g_dispatcher.config.max_events_per_cycle;
    }

    uint32_t processed = 0;
    while (processed < max_events) {
        if (event_dispatcher_process_one(K_NO_WAIT) != EVENT_OK) {
            break;  /* No more events */
        }
        processed++;
    }

    return processed;
}

/* =============================================================================
 * Statistics API
 * ============================================================================= */

void event_dispatcher_get_stats(dispatcher_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    *stats = g_dispatcher.stats;
    k_mutex_unlock(&g_dispatcher.lock);
}

void event_dispatcher_reset_stats(void)
{
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    memset(&g_dispatcher.stats, 0, sizeof(g_dispatcher.stats));
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_DBG("Dispatcher statistics reset");
}

uint32_t event_dispatcher_get_current_latency(void)
{
    return calculate_latency_us(g_dispatcher.last_event_time);
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void dispatcher_thread_func(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Dispatcher thread running");

    while (g_dispatcher.state == DISPATCHER_RUNNING) {
        /* Wait for event or timeout */
        k_sem_take(&g_dispatcher.sem, K_MSEC(100));

        /* Process batch of events */
        uint32_t processed = event_dispatcher_process_all(0);
        
        if (processed == 0) {
            /* No events, check if we should continue */
            if (g_dispatcher.state != DISPATCHER_RUNNING) {
                break;
            }
        }
    }

    LOG_INF("Dispatcher thread exiting");
}

static void process_event(const event_t *event)
{
    if (event == NULL) {
        return;
    }

    uint64_t start_time = k_cycle_get_64();

    /* Call event system to notify subscribers */
    extern event_status_t event_dispatch_internal(const event_t *event);
    event_status_t status = event_dispatch_internal(event);

    uint64_t end_time = k_cycle_get_64();
    uint32_t latency_us = (uint32_t)((end_time - start_time) * 1000000 / sys_clock_hw_cycles_per_sec);

    /* Update statistics */
    if (g_dispatcher.config.enable_stats) {
        k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
        
        g_dispatcher.stats.events_processed++;
        
        if (status != EVENT_OK) {
            g_dispatcher.stats.processing_errors++;
        }

        if (latency_us > g_dispatcher.stats.max_latency_us) {
            g_dispatcher.stats.max_latency_us = latency_us;
        }

        /* Running average */
        uint64_t total = (uint64_t)g_dispatcher.stats.avg_latency_us * 
                         (g_dispatcher.stats.events_processed - 1) + latency_us;
        g_dispatcher.stats.avg_latency_us = (uint32_t)(total / g_dispatcher.stats.events_processed);

        k_mutex_unlock(&g_dispatcher.lock);
    }

    LOG_DBG("Processed event type=%d, latency=%dus", event->type, latency_us);
}

static uint32_t calculate_latency_us(uint64_t event_timestamp)
{
    uint64_t now = k_uptime_get();
    uint64_t delta_ms = now - event_timestamp;
    return (uint32_t)(delta_ms * 1000);  /* ms to us */
}

/* =============================================================================
 * Event System Integration
 * ============================================================================= */

/* This function is called by event_system.c */
event_status_t event_dispatch_internal(const event_t *event)
{
    /* Forward to event_system's subscriber notification */
    extern event_status_t event_notify_subscribers(const event_t *event);
    return event_notify_subscribers(event);
}

struct k_msgq *event_system_get_queue(void)
{
    extern struct k_msgq g_event_msgq;
    return &g_event_msgq;
}
