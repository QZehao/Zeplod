/**
 * @file sys_timer.h
 * @brief 系统定时器服务头文件
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

#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Timer handle (opaque)
 */
typedef struct sys_timer* sys_timer_handle_t;

/**
 * @brief Timer mode
 */
typedef enum {
    SYS_TIMER_ONESHOT = 0, /* Fire once */
    SYS_TIMER_PERIODIC     /* Fire repeatedly */
} sys_timer_mode_t;

/**
 * @brief Timer status
 */
typedef enum { SYS_TIMER_STOPPED = 0, SYS_TIMER_RUNNING, SYS_TIMER_PAUSED, SYS_TIMER_EXPIRED } sys_timer_status_t;

/**
 * @brief Timer callback function
 * @param timer Timer handle
 * @param user_data User-defined data
 */
typedef void (*sys_timer_callback_t)(sys_timer_handle_t timer, void* user_data);

/**
 * @brief Timer configuration
 */
typedef struct {
    sys_timer_mode_t     mode;
    uint32_t             delay_ms;  /* Initial delay (or one-shot time) */
    uint32_t             period_ms; /* Period for periodic timers */
    sys_timer_callback_t callback;
    void*                user_data;
    const char*          name;
    int                  priority; /* Timer thread priority */
} sys_timer_config_t;

/**
 * @brief Timer statistics
 */
typedef struct {
    uint32_t fire_count;
    uint32_t miss_count;
    uint32_t last_fire_time_ms;
    uint32_t avg_latency_us;
    uint32_t max_latency_us;
} sys_timer_stats_t;

/* =============================================================================
 * Core API
 * ============================================================================= */

/**
 * @brief Initialize timer system
 * @return 0 on success, negative error code on failure
 */
int sys_timer_init(void);

/**
 * @brief Create a new timer
 * @param config Timer configuration
 * @return Timer handle, NULL on failure
 */
sys_timer_handle_t sys_timer_create(const sys_timer_config_t* config);

/**
 * @brief Delete a timer
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_delete(sys_timer_handle_t timer);

/**
 * @brief Start a timer
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_start(sys_timer_handle_t timer);

/**
 * @brief Stop a timer
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_stop(sys_timer_handle_t timer);

/**
 * @brief Restart a timer (stop then start)
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_restart(sys_timer_handle_t timer);

/**
 * @brief Pause a timer
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_pause(sys_timer_handle_t timer);

/**
 * @brief Resume a paused timer
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_resume(sys_timer_handle_t timer);

/**
 * @brief Get timer status
 * @param timer Timer handle
 * @return Current timer status
 */
sys_timer_status_t sys_timer_get_status(sys_timer_handle_t timer);

/**
 * @brief Modify timer period (for periodic timers)
 * @param timer Timer handle
 * @param period_ms New period in milliseconds
 * @return 0 on success, negative error code on failure
 */
int sys_timer_set_period(sys_timer_handle_t timer, uint32_t period_ms);

/**
 * @brief Get time until next expiration
 * @param timer Timer handle
 * @return Time in milliseconds (0 if not running)
 */
uint32_t sys_timer_get_time_until_expiry(sys_timer_handle_t timer);

/* =============================================================================
 * Statistics API
 * ============================================================================= */

/**
 * @brief Get timer statistics
 * @param timer Timer handle
 * @param stats Output: statistics structure
 * @return 0 on success, negative error code on failure
 */
int sys_timer_get_stats(sys_timer_handle_t timer, sys_timer_stats_t* stats);

/**
 * @brief Reset timer statistics
 * @param timer Timer handle
 * @return 0 on success, negative error code on failure
 */
int sys_timer_reset_stats(sys_timer_handle_t timer);

/* =============================================================================
 * Convenience Functions
 * ============================================================================= */

/**
 * @brief Create and start a one-shot timer
 * @param delay_ms Delay in milliseconds
 * @param callback Callback function
 * @param user_data User data for callback
 * @return Timer handle, NULL on failure
 */
sys_timer_handle_t sys_timer_oneshot(uint32_t delay_ms, sys_timer_callback_t callback, void* user_data);

/**
 * @brief Create and start a periodic timer
 * @param period_ms Period in milliseconds
 * @param callback Callback function
 * @param user_data User data for callback
 * @return Timer handle, NULL on failure
 */
sys_timer_handle_t sys_timer_periodic(uint32_t period_ms, sys_timer_callback_t callback, void* user_data);

/**
 * @brief Sleep for specified time (convenience wrapper)
 * @param ms Milliseconds to sleep
 */
void sys_timer_sleep(uint32_t ms);

/**
 * @brief Get system uptime in milliseconds
 * @return Uptime in milliseconds
 */
uint32_t sys_timer_get_uptime(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_TIMER_H */
