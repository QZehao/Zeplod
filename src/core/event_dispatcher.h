/**
 * @file event_dispatcher.h
 * @brief Event Dispatcher Header
 * 
 * High-performance event dispatcher with priority scheduling.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#include "event_system.h"
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
 * @brief Dispatcher state
 */
typedef enum {
    DISPATCHER_STOPPED = 0,
    DISPATCHER_RUNNING,
    DISPATCHER_PAUSED,
    DISPATCHER_ERROR
} dispatcher_state_t;

/**
 * @brief Dispatcher configuration
 */
typedef struct {
    uint32_t stack_size;
    int priority;
    const char *thread_name;
    bool enable_stats;
    uint32_t max_events_per_cycle;
} dispatcher_config_t;

/**
 * @brief Dispatcher statistics
 */
typedef struct {
    uint64_t events_processed;
    uint64_t events_dropped;
    uint32_t max_latency_us;
    uint32_t avg_latency_us;
    uint32_t processing_errors;
} dispatcher_stats_t;

/**
 * @brief Event filter function
 * @param event Event to filter
 * @param user_data User data
 * @return true to process event, false to skip
 */
typedef bool (*event_filter_t)(const event_t *event, void *user_data);

/* =============================================================================
 * Dispatcher Control API
 * ============================================================================= */

/**
 * @brief Initialize event dispatcher
 * @param config Dispatcher configuration
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_dispatcher_init(const dispatcher_config_t *config);

/**
 * @brief Start the dispatcher
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_dispatcher_start(void);

/**
 * @brief Stop the dispatcher
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_dispatcher_stop(void);

/**
 * @brief Pause event processing
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_dispatcher_pause(void);

/**
 * @brief Resume event processing
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_dispatcher_resume(void);

/**
 * @brief Get dispatcher state
 * @return Current dispatcher state
 */
dispatcher_state_t event_dispatcher_get_state(void);

/* =============================================================================
 * Event Processing API
 * ============================================================================= */

/**
 * @brief Set event filter
 * @param filter Filter function
 * @param user_data User data for filter
 */
void event_dispatcher_set_filter(event_filter_t filter, void *user_data);

/**
 * @brief Clear event filter
 */
void event_dispatcher_clear_filter(void);

/**
 * @brief Process single event (for manual dispatch)
 * @param timeout Wait timeout
 * @return EVENT_OK if event processed, error code otherwise
 */
event_status_t event_dispatcher_process_one(k_timeout_t timeout);

/**
 * @brief Process all pending events
 * @param max_events Maximum events to process (0 = unlimited)
 * @return Number of events processed
 */
uint32_t event_dispatcher_process_all(uint32_t max_events);

/* =============================================================================
 * Statistics API
 * ============================================================================= */

/**
 * @brief Get dispatcher statistics
 * @param stats Output: statistics structure
 */
void event_dispatcher_get_stats(dispatcher_stats_t *stats);

/**
 * @brief Reset dispatcher statistics
 */
void event_dispatcher_reset_stats(void);

/**
 * @brief Get current event latency
 * @return Latency in microseconds
 */
uint32_t event_dispatcher_get_current_latency(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_DISPATCHER_H */
