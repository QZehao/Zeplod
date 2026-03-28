/**
 * @file event_queue.h
 * @brief Event Queue Management Header
 * 
 * Provides priority-based event queuing with overflow handling.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include "event_system.h"
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Configuration
 * ============================================================================= */

#ifndef CONFIG_EVENT_QUEUE_HIGH_WATERMARK
#define CONFIG_EVENT_QUEUE_HIGH_WATERMARK  (CONFIG_EVENT_QUEUE_SIZE * 3 / 4)
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Queue overflow policy
 */
typedef enum {
    QUEUE_OVERFLOW_DROP_LOWEST,   /* Drop lowest priority events */
    QUEUE_OVERFLOW_DROP_NEWEST,   /* Drop new events */
    QUEUE_OVERFLOW_BLOCK          /* Block on full (not recommended for RT) */
} queue_overflow_policy_t;

/**
 * @brief Queue statistics
 */
typedef struct {
    uint32_t enqueue_count;
    uint32_t dequeue_count;
    uint32_t overflow_count;
    uint32_t drop_count;
    uint32_t high_watermark;
} queue_stats_t;

/* =============================================================================
 * Queue API
 * ============================================================================= */

/**
 * @brief Initialize event queue
 * @param queue Pointer to queue structure
 * @param buffer Buffer for queue storage
 * @param capacity Maximum queue capacity
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_queue_init(struct k_msgq *queue, void *buffer, size_t capacity);

/**
 * @brief Enqueue an event
 * @param queue Queue instance
 * @param event Event to enqueue
 * @param policy Overflow policy
 * @param timeout Wait timeout
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_queue_enqueue(struct k_msgq *queue, 
                                    const event_t *event,
                                    queue_overflow_policy_t policy,
                                    k_timeout_t timeout);

/**
 * @brief Dequeue an event
 * @param queue Queue instance
 * @param event Output: dequeued event
 * @param timeout Wait timeout
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_queue_dequeue(struct k_msgq *queue,
                                    event_t *event,
                                    k_timeout_t timeout);

/**
 * @brief Check if queue is empty
 * @param queue Queue instance
 * @return true if empty, false otherwise
 */
bool event_queue_is_empty(const struct k_msgq *queue);

/**
 * @brief Check if queue is full
 * @param queue Queue instance
 * @return true if full, false otherwise
 */
bool event_queue_is_full(const struct k_msgq *queue);

/**
 * @brief Get queue depth
 * @param queue Queue instance
 * @return Number of events in queue
 */
uint32_t event_queue_depth(const struct k_msgq *queue);

/**
 * @brief Get queue capacity
 * @param queue Queue instance
 * @return Maximum queue capacity
 */
uint32_t event_queue_capacity(const struct k_msgq *queue);

/**
 * @brief Purge all events from queue
 * @param queue Queue instance
 */
void event_queue_purge(struct k_msgq *queue);

/**
 * @brief Get queue statistics
 * @param queue Queue instance
 * @param stats Output: statistics structure
 */
void event_queue_get_stats(const struct k_msgq *queue, queue_stats_t *stats);

/**
 * @brief Reset queue statistics
 * @param queue Queue instance
 */
void event_queue_reset_stats(struct k_msgq *queue);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_QUEUE_H */
