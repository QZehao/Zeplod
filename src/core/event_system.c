/**
 * @file event_system.c
 * @brief Core Event System Implementation
 * 
 * Thread-safe, high-performance event system with publish-subscribe pattern.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "event_system.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(event_system, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#define MAX_EVENT_TYPES     256
#define EVENT_SYSTEM_MAGIC  0x45564E54  /* "EVNT" */

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

/**
 * @brief Event system control block
 */
typedef struct {
    uint32_t                magic;
    bool                    initialized;
    bool                    running;
    struct k_thread         dispatcher_thread;
    K_THREAD_STACK_MEMBER(dispatcher_stack, CONFIG_EVENT_DISPATCHER_STACK_SIZE);
    struct k_msgq          *event_queue;
    event_type_entry_t      event_types[MAX_EVENT_TYPES];
    uint32_t                total_events;
    uint32_t                dropped_events;
    struct k_mutex          stats_lock;
    uint32_t                next_subscriber_id;
} event_system_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static event_system_cb_t g_event_system;
static struct k_msgq g_event_msgq;
static char g_event_msgq_buffer[CONFIG_EVENT_QUEUE_SIZE * sizeof(event_t)];

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void event_dispatcher_thread(void *p1, void *p2, void *p3);
static event_status_t notify_subscribers(const event_t *event);
static subscriber_entry_t *find_subscriber(event_type_entry_t *entry, uint32_t subscriber_id);

/* =============================================================================
 * Core Implementation
 * ============================================================================= */

event_status_t event_system_init(void)
{
    LOG_INF("Initializing event system...");

    if (g_event_system.initialized) {
        LOG_WRN("Event system already initialized");
        return EVENT_OK;
    }

    /* Initialize control block */
    memset(&g_event_system, 0, sizeof(g_event_system));
    g_event_system.magic = EVENT_SYSTEM_MAGIC;

    /* Initialize mutexes */
    k_mutex_init(&g_event_system.stats_lock);

    /* Initialize event type entries */
    for (int i = 0; i < MAX_EVENT_TYPES; i++) {
        g_event_system.event_types[i].type = i;
        k_mutex_init(&g_event_system.event_types[i].lock);
    }

    /* Initialize message queue */
    k_msgq_init(&g_event_msgq, g_event_msgq_buffer, sizeof(event_t), CONFIG_EVENT_QUEUE_SIZE);
    g_event_system.event_queue = &g_event_msgq;

    g_event_system.next_subscriber_id = 1;
    g_event_system.initialized = true;

    LOG_INF("Event system initialized successfully");
    return EVENT_OK;
}

event_status_t event_system_start(void)
{
    if (!g_event_system.initialized) {
        LOG_ERR("Event system not initialized");
        return EVENT_ERR_INVALID_ARG;
    }

    if (g_event_system.running) {
        LOG_WRN("Event system already running");
        return EVENT_OK;
    }

    /* Create dispatcher thread */
    k_thread_create(&g_event_system.dispatcher_thread,
                    g_event_system.dispatcher_stack,
                    CONFIG_EVENT_DISPATCHER_STACK_SIZE,
                    event_dispatcher_thread,
                    NULL, NULL, NULL,
                    CONFIG_EVENT_DISPATCHER_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&g_event_system.dispatcher_thread, "event_disp");

    g_event_system.running = true;
    LOG_INF("Event system started");
    return EVENT_OK;
}

event_status_t event_system_stop(void)
{
    if (!g_event_system.running) {
        return EVENT_OK;
    }

    g_event_system.running = false;
    
    /* Wake up dispatcher thread */
    event_t dummy_event = {0};
    k_msgq_put(&g_event_msgq, &dummy_event, K_NO_WAIT);
    
    k_thread_abort(&g_event_system.dispatcher_thread);
    
    LOG_INF("Event system stopped");
    return EVENT_OK;
}

bool event_system_is_running(void)
{
    return g_event_system.running;
}

/* =============================================================================
 * Event Type Management
 * ============================================================================= */

event_status_t event_register_type(event_type_t type, const char *name)
{
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES) {
        LOG_ERR("Invalid event type: %d", type);
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t *entry = &g_event_system.event_types[type];
    
    k_mutex_lock(&entry->lock, K_FOREVER);
    
    if (entry->name != NULL) {
        k_mutex_unlock(&entry->lock);
        LOG_WRN("Event type %d already registered", type);
        return EVENT_OK;  /* Idempotent */
    }

    entry->name = name;
    entry->subscriber_count = 0;
    memset(entry->subscribers, 0, sizeof(entry->subscribers));
    
    k_mutex_unlock(&entry->lock);
    
    LOG_DBG("Registered event type: %s (%d)", name, type);
    return EVENT_OK;
}

event_status_t event_unregister_type(event_type_t type)
{
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t *entry = &g_event_system.event_types[type];
    
    k_mutex_lock(&entry->lock, K_FOREVER);
    
    if (entry->name == NULL) {
        k_mutex_unlock(&entry->lock);
        return EVENT_ERR_NOT_FOUND;
    }

    /* Check for active subscribers */
    if (entry->subscriber_count > 0) {
        k_mutex_unlock(&entry->lock);
        LOG_WRN("Cannot unregister type %d with active subscribers", type);
        return EVENT_ERR_NO_SUBSCRIBER;
    }

    entry->name = NULL;
    entry->subscriber_count = 0;
    
    k_mutex_unlock(&entry->lock);
    
    LOG_DBG("Unregistered event type: %d", type);
    return EVENT_OK;
}

/* =============================================================================
 * Subscription Management
 * ============================================================================= */

event_status_t event_subscribe(event_type_t type,
                                event_callback_t callback,
                                void *user_data,
                                uint32_t *subscriber_id)
{
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES || callback == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t *entry = &g_event_system.event_types[type];
    
    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Find empty slot */
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        if (!entry->subscribers[i].is_active) {
            entry->subscribers[i].callback = callback;
            entry->subscribers[i].user_data = user_data;
            entry->subscribers[i].subscriber_id = g_event_system.next_subscriber_id;
            entry->subscribers[i].is_active = true;
            entry->subscriber_count++;
            
            if (subscriber_id != NULL) {
                *subscriber_id = entry->subscribers[i].subscriber_id;
            }
            
            g_event_system.next_subscriber_id++;
            
            k_mutex_unlock(&entry->lock);
            LOG_DBG("Subscriber %d registered for event type %d", 
                    entry->subscribers[i].subscriber_id, type);
            return EVENT_OK;
        }
    }

    k_mutex_unlock(&entry->lock);
    LOG_ERR("No room for more subscribers on event type %d", type);
    return EVENT_ERR_QUEUE_FULL;
}

event_status_t event_unsubscribe(event_type_t type, uint32_t subscriber_id)
{
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES || subscriber_id == 0) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t *entry = &g_event_system.event_types[type];
    
    k_mutex_lock(&entry->lock, K_FOREVER);

    subscriber_entry_t *sub = find_subscriber(entry, subscriber_id);
    if (sub == NULL) {
        k_mutex_unlock(&entry->lock);
        return EVENT_ERR_NOT_FOUND;
    }

    sub->is_active = false;
    sub->callback = NULL;
    sub->user_data = NULL;
    entry->subscriber_count--;
    
    k_mutex_unlock(&entry->lock);
    LOG_DBG("Subscriber %d removed from event type %d", subscriber_id, type);
    return EVENT_OK;
}

void event_unsubscribe_all(uint32_t subscriber_id)
{
    if (!g_event_system.initialized || subscriber_id == 0) {
        return;
    }

    for (event_type_t type = 0; type < MAX_EVENT_TYPES; type++) {
        event_type_entry_t *entry = &g_event_system.event_types[type];
        
        if (entry->name == NULL) {
            continue;  /* Skip unregistered types */
        }
        
        k_mutex_lock(&entry->lock, K_FOREVER);
        
        subscriber_entry_t *sub = find_subscriber(entry, subscriber_id);
        if (sub != NULL) {
            sub->is_active = false;
            sub->callback = NULL;
            sub->user_data = NULL;
            entry->subscriber_count--;
        }
        
        k_mutex_unlock(&entry->lock);
    }
    
    LOG_DBG("Subscriber %d removed from all event types", subscriber_id);
}

/* =============================================================================
 * Event Publishing
 * ============================================================================= */

event_status_t event_publish(const event_t *event)
{
    if (!g_event_system.initialized || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (!g_event_system.running) {
        LOG_WRN("Event system not running, event dropped");
        return EVENT_ERR_INVALID_ARG;
    }

    /* Validate event type */
    if (event->type >= MAX_EVENT_TYPES || 
        g_event_system.event_types[event->type].name == NULL) {
        LOG_WRN("Publishing to unregistered event type: %d", event->type);
    }

    int ret = k_msgq_put(g_event_system.event_queue, event, K_NO_WAIT);
    if (ret != 0) {
        k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
        g_event_system.dropped_events++;
        k_mutex_unlock(&g_event_system.stats_lock);
        
        LOG_WRN("Event queue full, event dropped (type=%d)", event->type);
        return EVENT_ERR_QUEUE_FULL;
    }

    return EVENT_OK;
}

event_status_t event_publish_from_isr(const event_t *event)
{
    if (!g_event_system.initialized || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    int ret = k_msgq_put(g_event_system.event_queue, event, K_NO_WAIT);
    if (ret != 0) {
        k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
        g_event_system.dropped_events++;
        k_mutex_unlock(&g_event_system.stats_lock);
        
        return EVENT_ERR_QUEUE_FULL;
    }

    return EVENT_OK;
}

event_status_t event_publish_copy(event_type_t type,
                                   event_priority_t priority,
                                   const void *data,
                                   size_t data_len)
{
    event_t *event = event_create_with_data(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);
    event_free(event);  /* Event system made a copy in the queue */
    
    return status;
}

/* =============================================================================
 * Event Creation & Memory Management
 * ============================================================================= */

event_t *event_create(event_type_t type, event_priority_t priority)
{
    if (!g_event_system.initialized) {
        return NULL;
    }

    event_t *event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        LOG_ERR("Failed to allocate event");
        return NULL;
    }

    memset(event, 0, sizeof(event_t));
    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->is_dynamic = true;

    return event;
}

event_t *event_create_with_data(event_type_t type,
                                 event_priority_t priority,
                                 const void *data,
                                 size_t data_len)
{
    if (data == NULL || data_len == 0) {
        return event_create(type, priority);
    }

    event_t *event = event_create(type, priority);
    if (event == NULL) {
        return NULL;
    }

    event->data = k_malloc(data_len);
    if (event->data == NULL) {
        k_free(event);
        LOG_ERR("Failed to allocate event data");
        return NULL;
    }

    memcpy(event->data, data, data_len);
    event->data_len = data_len;

    return event;
}

void event_free(event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (event->is_dynamic && event->data != NULL) {
        k_free(event->data);
    }
    k_free(event);
}

/* =============================================================================
 * Utility Functions
 * ============================================================================= */

const char *event_get_type_name(event_type_t type)
{
    if (!g_event_system.initialized || type >= MAX_EVENT_TYPES) {
        return "UNKNOWN";
    }

    const char *name = g_event_system.event_types[type].name;
    return name != NULL ? name : "UNREGISTERED";
}

uint32_t event_get_subscriber_count(event_type_t type)
{
    if (!g_event_system.initialized || type >= MAX_EVENT_TYPES) {
        return 0;
    }

    event_type_entry_t *entry = &g_event_system.event_types[type];
    k_mutex_lock(&entry->lock, K_FOREVER);
    uint32_t count = entry->subscriber_count;
    k_mutex_unlock(&entry->lock);
    
    return count;
}

void event_get_statistics(uint32_t *total_events,
                          uint32_t *queue_depth,
                          uint32_t *dropped_events)
{
    if (!g_event_system.initialized) {
        return;
    }

    k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
    
    if (total_events != NULL) {
        *total_events = g_event_system.total_events;
    }
    if (queue_depth != NULL) {
        *queue_depth = k_msgq_num_used_get(g_event_system.event_queue);
    }
    if (dropped_events != NULL) {
        *dropped_events = g_event_system.dropped_events;
    }
    
    k_mutex_unlock(&g_event_system.stats_lock);
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void event_dispatcher_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Event dispatcher thread started");

    while (g_event_system.running) {
        event_t event;
        
        /* Wait for event with timeout to allow checking running flag */
        int ret = k_msgq_get(g_event_system.event_queue, &event, K_MSEC(100));
        
        if (ret != 0) {
            continue;  /* Timeout, check running flag */
        }

        /* Process event */
        notify_subscribers(&event);

        k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
        g_event_system.total_events++;
        k_mutex_unlock(&g_event_system.stats_lock);

        /* Free dynamic data if event was copied */
        if (event.is_dynamic && event.data != NULL) {
            k_free(event.data);
        }
    }

    LOG_INF("Event dispatcher thread stopped");
}

static event_status_t notify_subscribers(const event_t *event)
{
    if (event->type >= MAX_EVENT_TYPES) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t *entry = &g_event_system.event_types[event->type];
    
    k_mutex_lock(&entry->lock, K_FOREVER);

    if (entry->subscriber_count == 0) {
        k_mutex_unlock(&entry->lock);
        return EVENT_ERR_NO_SUBSCRIBER;
    }

    /* Notify all active subscribers */
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        subscriber_entry_t *sub = &entry->subscribers[i];
        
        if (sub->is_active && sub->callback != NULL) {
            /* Call subscriber callback */
            sub->callback(event, sub->user_data);
        }
    }

    k_mutex_unlock(&entry->lock);
    return EVENT_OK;
}

static subscriber_entry_t *find_subscriber(event_type_entry_t *entry, uint32_t subscriber_id)
{
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        if (entry->subscribers[i].is_active &&
            entry->subscribers[i].subscriber_id == subscriber_id) {
            return &entry->subscribers[i];
        }
    }
    return NULL;
}
