/**
 * @file event_system.h
 * @brief Core Event System Header
 * 
 * High-performance, real-time event system for Zephyr RTOS.
 * Provides publish-subscribe pattern with thread-safe operations.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef EVENT_SYSTEM_H
#define EVENT_SYSTEM_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Configuration
 * ============================================================================= */

#ifndef CONFIG_EVENT_QUEUE_SIZE
#define CONFIG_EVENT_QUEUE_SIZE 64
#endif

#ifndef CONFIG_EVENT_MAX_SUBSCRIBERS
#define CONFIG_EVENT_MAX_SUBSCRIBERS 16
#endif

#ifndef CONFIG_EVENT_DISPATCHER_STACK_SIZE
#define CONFIG_EVENT_DISPATCHER_STACK_SIZE 2048
#endif

#ifndef CONFIG_EVENT_DISPATCHER_PRIORITY
#define CONFIG_EVENT_DISPATCHER_PRIORITY 5
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Event type identifier (supports up to 256 event types)
 */
typedef uint8_t event_type_t;

/**
 * @brief Event priority levels
 */
typedef enum {
    EVENT_PRIORITY_LOW      = 10,
    EVENT_PRIORITY_NORMAL   = 5,
    EVENT_PRIORITY_HIGH     = 2,
    EVENT_PRIORITY_CRITICAL = 0
} event_priority_t;

/**
 * @brief Event status codes
 */
typedef enum {
    EVENT_OK                = 0,
    EVENT_ERR_NO_MEM        = -1,
    EVENT_ERR_QUEUE_FULL    = -2,
    EVENT_ERR_QUEUE_EMPTY   = -3,
    EVENT_ERR_INVALID_ARG   = -4,
    EVENT_ERR_NOT_FOUND     = -5,
    EVENT_ERR_NO_SUBSCRIBER = -6,
    EVENT_ERR_TIMEOUT       = -7
} event_status_t;

/**
 * @brief Event data structure
 */
typedef struct {
    event_type_t    type;           /**< Event type identifier */
    event_priority_t priority;      /**< Event priority */
    uint32_t        timestamp;      /**< Event creation timestamp (ticks) */
    uint32_t        source_id;      /**< Source module/component ID */
    uint32_t        data_len;       /**< Length of event data */
    void           *data;           /**< Pointer to event data */
    bool            is_dynamic;     /**< True if data was dynamically allocated */
} event_t;

/**
 * @brief Event callback function type
 * @param event Pointer to the event
 * @param user_data User-defined data passed during subscription
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief Subscriber entry
 */
typedef struct {
    event_callback_t callback;      /**< Callback function */
    void            *user_data;     /**< User data for callback */
    uint32_t         subscriber_id; /**< Unique subscriber ID */
    bool             is_active;     /**< Subscriber is active */
} subscriber_entry_t;

/**
 * @brief Event type registration
 */
typedef struct {
    event_type_t       type;                  /**< Event type ID */
    const char        *name;                  /**< Event type name */
    subscriber_entry_t subscribers[CONFIG_EVENT_MAX_SUBSCRIBERS];
    uint32_t           subscriber_count;
    struct k_mutex     lock;
} event_type_entry_t;

/* =============================================================================
 * Core API
 * ============================================================================= */

/**
 * @brief Initialize the event system
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_system_init(void);

/**
 * @brief Start the event dispatcher
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_system_start(void);

/**
 * @brief Stop the event dispatcher
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_system_stop(void);

/**
 * @brief Check if event system is running
 * @return true if running, false otherwise
 */
bool event_system_is_running(void);

/* =============================================================================
 * Event Type Management
 * ============================================================================= */

/**
 * @brief Register a new event type
 * @param type Event type ID
 * @param name Event type name
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_register_type(event_type_t type, const char *name);

/**
 * @brief Unregister an event type
 * @param type Event type ID
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_unregister_type(event_type_t type);

/* =============================================================================
 * Subscription Management
 * ============================================================================= */

/**
 * @brief Subscribe to an event type
 * @param type Event type ID
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @param subscriber_id Output: assigned subscriber ID
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_subscribe(event_type_t type, 
                                event_callback_t callback,
                                void *user_data,
                                uint32_t *subscriber_id);

/**
 * @brief Unsubscribe from an event type
 * @param type Event type ID
 * @param subscriber_id Subscriber ID to remove
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_unsubscribe(event_type_t type, uint32_t subscriber_id);

/**
 * @brief Unsubscribe from all events
 * @param subscriber_id Subscriber ID to remove from all types
 */
void event_unsubscribe_all(uint32_t subscriber_id);

/* =============================================================================
 * Event Publishing
 * ============================================================================= */

/**
 * @brief Publish an event (synchronous)
 * @param event Event to publish
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_publish(const event_t *event);

/**
 * @brief Publish an event from ISR context
 * @param event Event to publish
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_publish_from_isr(const event_t *event);

/**
 * @brief Publish event with data copy (creates internal copy)
 * @param type Event type ID
 * @param priority Event priority
 * @param data Data to copy
 * @param data_len Length of data
 * @return EVENT_OK on success, error code otherwise
 */
event_status_t event_publish_copy(event_type_t type,
                                   event_priority_t priority,
                                   const void *data,
                                   size_t data_len);

/* =============================================================================
 * Event Creation & Memory Management
 * ============================================================================= */

/**
 * @brief Create a new event
 * @param type Event type ID
 * @param priority Event priority
 * @return Pointer to new event, NULL on failure
 */
event_t *event_create(event_type_t type, event_priority_t priority);

/**
 * @brief Create event with data
 * @param type Event type ID
 * @param priority Event priority
 * @param data Data to attach
 * @param data_len Length of data
 * @return Pointer to new event, NULL on failure
 */
event_t *event_create_with_data(event_type_t type,
                                 event_priority_t priority,
                                 const void *data,
                                 size_t data_len);

/**
 * @brief Free an event
 * @param event Event to free
 */
void event_free(event_t *event);

/* =============================================================================
 * Utility Functions
 * ============================================================================= */

/**
 * @brief Get event type name
 * @param type Event type ID
 * @return Event type name string
 */
const char *event_get_type_name(event_type_t type);

/**
 * @brief Get number of subscribers for an event type
 * @param type Event type ID
 * @return Number of subscribers
 */
uint32_t event_get_subscriber_count(event_type_t type);

/**
 * @brief Get event system statistics
 * @param total_events Output: total events processed
 * @param queue_depth Output: current queue depth
 * @param dropped_events Output: number of dropped events
 */
void event_get_statistics(uint32_t *total_events, 
                          uint32_t *queue_depth,
                          uint32_t *dropped_events);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SYSTEM_H */
