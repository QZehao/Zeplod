/**
 * @file event_queue.c
 * @brief Event Queue Implementation
 * 
 * Priority-based event queue with configurable overflow handling.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "event_queue.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(event_queue, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

/**
 * @brief Extended queue control block
 */
typedef struct {
    struct k_msgq *msgq;
    queue_stats_t stats;
    uint32_t capacity;
    struct k_mutex stats_lock;
} event_queue_cb_t;

/* Static queue control blocks for tracking stats */
static event_queue_cb_t g_queue_cb[CONFIG_EVENT_QUEUE_SIZE > 256 ? 2 : 1];

/* =============================================================================
 * Queue API Implementation
 * ============================================================================= */

event_status_t event_queue_init(struct k_msgq *queue, void *buffer, size_t capacity)
{
    if (queue == NULL || buffer == NULL || capacity == 0) {
        return EVENT_ERR_INVALID_ARG;
    }

    k_msgq_init(queue, buffer, sizeof(event_t), capacity);
    
    /* Initialize stats */
    event_queue_cb_t *cb = &g_queue_cb[0];
    cb->msgq = queue;
    cb->capacity = capacity;
    cb->stats = (queue_stats_t){0};
    k_mutex_init(&cb->stats_lock);
    
    LOG_DBG("Event queue initialized: capacity=%d", capacity);
    return EVENT_OK;
}

event_status_t event_queue_enqueue(struct k_msgq *queue,
                                    const event_t *event,
                                    queue_overflow_policy_t policy,
                                    k_timeout_t timeout)
{
    if (queue == NULL || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_queue_cb_t *cb = &g_queue_cb[0];
    
    /* Check if queue is full */
    if (k_msgq_num_used_get(queue) >= cb->capacity) {
        k_mutex_lock(&cb->stats_lock, K_FOREVER);
        cb->stats.overflow_count++;
        k_mutex_unlock(&cb->stats_lock);
        
        switch (policy) {
            case QUEUE_OVERFLOW_DROP_NEWEST:
                LOG_DBG("Queue full, dropping newest event");
                return EVENT_ERR_QUEUE_FULL;
                
            case QUEUE_OVERFLOW_DROP_LOWEST:
                /* TODO: Implement priority-based drop */
                LOG_DBG("Queue full, would drop lowest priority");
                /* Fall through to block for now */
                break;
                
            case QUEUE_OVERFLOW_BLOCK:
                /* Will block below */
                break;
        }
    }

    int ret = k_msgq_put(queue, event, timeout);
    if (ret != 0) {
        if (ret == -ENOMSG) {
            return EVENT_ERR_QUEUE_FULL;
        }
        return EVENT_ERR_TIMEOUT;
    }

    /* Update stats */
    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.enqueue_count++;
    
    uint32_t current_depth = k_msgq_num_used_get(queue);
    if (current_depth > cb->stats.high_watermark) {
        cb->stats.high_watermark = current_depth;
    }
    k_mutex_unlock(&cb->stats_lock);

    return EVENT_OK;
}

event_status_t event_queue_dequeue(struct k_msgq *queue,
                                    event_t *event,
                                    k_timeout_t timeout)
{
    if (queue == NULL || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    int ret = k_msgq_get(queue, event, timeout);
    if (ret != 0) {
        if (ret == -ENOMSG) {
            return EVENT_ERR_QUEUE_EMPTY;
        }
        return EVENT_ERR_TIMEOUT;
    }

    /* Update stats */
    event_queue_cb_t *cb = &g_queue_cb[0];
    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.dequeue_count++;
    k_mutex_unlock(&cb->stats_lock);

    return EVENT_OK;
}

bool event_queue_is_empty(const struct k_msgq *queue)
{
    return k_msgq_num_used_get(queue) == 0;
}

bool event_queue_is_full(const struct k_msgq *queue)
{
    return k_msgq_num_used_get(queue) == k_msgq_max_msgs_get(queue);
}

uint32_t event_queue_depth(const struct k_msgq *queue)
{
    return k_msgq_num_used_get(queue);
}

uint32_t event_queue_capacity(const struct k_msgq *queue)
{
    return k_msgq_max_msgs_get(queue);
}

void event_queue_purge(struct k_msgq *queue)
{
    k_msgq_purge(queue);
    LOG_DBG("Event queue purged");
}

void event_queue_get_stats(const struct k_msgq *queue, queue_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    event_queue_cb_t *cb = &g_queue_cb[0];
    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    *stats = cb->stats;
    k_mutex_unlock(&cb->stats_lock);
}

void event_queue_reset_stats(struct k_msgq *queue)
{
    event_queue_cb_t *cb = &g_queue_cb[0];
    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats = (queue_stats_t){0};
    k_mutex_unlock(&cb->stats_lock);
    
    LOG_DBG("Queue statistics reset");
}
