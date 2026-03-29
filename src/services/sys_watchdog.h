/**
 * @file sys_watchdog.h
 * @brief System Watchdog Service Header
 * 
 * Hardware/software watchdog for system reliability and fault recovery.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYS_WATCHDOG_H
#define SYS_WATCHDOG_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Watchdog status
 */
typedef enum {
    WDT_STATUS_STOPPED = 0,
    WDT_STATUS_RUNNING,
    WDT_STATUS_PAUSED,
    WDT_STATUS_EXPIRED,
    WDT_STATUS_ERROR
} wdt_status_t;

/**
 * @brief Watchdog mode
 */
typedef enum {
    WDT_MODE_SOFTWARE = 0,    /* Software watchdog (thread monitoring) */
    WDT_MODE_HARDWARE,        /* Hardware watchdog (if available) */
    WDT_MODE_DUAL             /* Both hardware and software */
} wdt_mode_t;

/**
 * @brief Watchdog user callback (before expiry); distinct from Zephyr wdt_callback_t
 */
typedef void (*sys_wdt_user_cb_t)(void *user_data);

/**
 * @brief Watchdog configuration
 */
typedef struct {
    wdt_mode_t mode;
    uint32_t timeout_ms;
    uint32_t feed_margin_ms;    /* Feed before this time to avoid race */
    sys_wdt_user_cb_t pre_expire_callback;
    void *callback_user_data;
    bool reset_on_expire;
    const char *name;
} wdt_config_t;

/**
 * @brief Watchdog statistics
 */
typedef struct {
    uint32_t feed_count;
    uint32_t warning_count;
    uint32_t expire_count;
    uint32_t reset_count;
    uint32_t max_feed_interval_ms;
    uint32_t last_feed_time_ms;
} wdt_stats_t;

/* =============================================================================
 * Core API
 * ============================================================================= */

/**
 * @brief Initialize watchdog system
 * @param config Watchdog configuration
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_init(const wdt_config_t *config);

/**
 * @brief Start watchdog
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_start(void);

/**
 * @brief Stop watchdog
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_stop(void);

/**
 * @brief Feed (kick) the watchdog
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_feed(void);

/**
 * @brief Pause watchdog (for debugging)
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_pause(void);

/**
 * @brief Resume watchdog
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_resume(void);

/**
 * @brief Get watchdog status
 * @return Current watchdog status
 */
wdt_status_t sys_wdt_get_status(void);

/* =============================================================================
 * Thread Monitoring API (Software Watchdog)
 * ============================================================================= */

/**
 * @brief Register a thread for monitoring
 * @param thread_id Thread identifier
 * @param thread_name Thread name
 * @param max_idle_ms Maximum allowed idle time
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_monitor_thread(k_tid_t thread_id, const char *thread_name, 
                           uint32_t max_idle_ms);

/**
 * @brief Unregister a thread from monitoring
 * @param thread_id Thread identifier
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_unmonitor_thread(k_tid_t thread_id);

/**
 * @brief Mark thread as alive (called by monitored thread)
 * @param thread_id Thread identifier
 * @return 0 on success, negative error code on failure
 */
int sys_wdt_thread_alive(k_tid_t thread_id);

/* =============================================================================
 * Statistics & Debug API
 * ============================================================================= */

/**
 * @brief Get watchdog statistics
 * @param stats Output: statistics structure
 */
void sys_wdt_get_stats(wdt_stats_t *stats);

/**
 * @brief Reset watchdog statistics
 */
void sys_wdt_reset_stats(void);

/**
 * @brief Get time since last feed
 * @return Time in milliseconds
 */
uint32_t sys_wdt_get_time_since_feed(void);

/**
 * @brief Get time until expiration
 * @return Time in milliseconds (0 if expired)
 */
uint32_t sys_wdt_get_time_until_expire(void);

/**
 * @brief Simulate watchdog expiration (for testing)
 */
void sys_wdt_simulate_expire(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_WATCHDOG_H */
