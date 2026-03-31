/**
 * @file event_system.c
 * @brief 核心事件系统实现 (Core Event System Implementation)
 *
 * 基于发布 - 订阅模式的线程安全高性能事件系统。
 *
 * 架构说明：
 * - 事件队列：使用 Zephyr k_msgq 实现，支持多生产者单消费者
 * - 事件分发：由 event_dispatcher 模块中的线程消费队列并调用 event_notify_subscribers
 * - 订阅管理：每个事件类型维护一个订阅者列表
 * - 线程安全：使用互斥锁保护共享数据结构
 * - ISR 支持：提供专门的中断上下文发布函数
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "event_system.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <stdatomic.h>

LOG_MODULE_REGISTER(event_system, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义 (Internal Definitions)
 * ============================================================================= */

/** 最大支持的事件类型数量 */
#define MAX_EVENT_TYPES    256

/** 魔术字，用于验证控制块有效性 ("EVNT") */
#define EVENT_SYSTEM_MAGIC 0x45564E54

/* =============================================================================
 * 内部数据结构 (Internal Data Structures)
 * ============================================================================= */

/**
 * @brief 事件系统控制块
 *
 * 包含事件系统的全局状态和数据结构。
 */
typedef struct {
    uint32_t           magic;                        /**< 魔术字，用于验证有效性 */
    bool               initialized;                  /**< 系统是否已初始化 */
    atomic_t           running;                      /**< 非 0：允许投递（线程与 ISR 可读） */
    struct k_msgq*     event_queue;                  /**< 事件队列指针 */
    event_type_entry_t event_types[MAX_EVENT_TYPES]; /**< 事件类型表 */
    uint32_t           total_events;                 /**< 已处理的事件总数 */
    struct k_mutex     stats_lock;                   /**< 保护统计信息的互斥锁 */
    uint32_t           next_subscriber_id;           /**< 下一个可用的订阅者 ID */
} event_system_cb_t;

/* =============================================================================
 * 静态变量 (Static Variables)
 * ============================================================================= */

/** 全局事件系统控制块实例 */
static event_system_cb_t g_event_system;

/** 全局事件消息队列 */
static struct k_msgq g_event_msgq;

/** 事件消息队列缓冲区 */
static char g_event_msgq_buffer[CONFIG_EVENT_QUEUE_SIZE * sizeof(event_t)];

/**
 * ISR 安全的丢弃计数器
 * 使用 atomic 避免在 event_publish_from_isr 中使用互斥锁
 */
static _Atomic uint32_t g_event_dropped_count;

/* =============================================================================
 * 前置声明 (Forward Declarations)
 * ============================================================================= */

/**
 * @brief 查找订阅者
 * @param entry 事件类型条目
 * @param subscriber_id 订阅者 ID
 * @return 指向订阅者条目的指针，未找到返回 NULL
 */
static subscriber_entry_t* find_subscriber(event_type_entry_t* entry, uint32_t subscriber_id);
static void                purge_event_queue_and_free_payload(struct k_msgq* queue);

/* =============================================================================
 * 核心实现 (Core Implementation)
 * ============================================================================= */

/**
 * @brief 初始化事件系统
 *
 * 初始化步骤：
 * 1. 检查是否已初始化（幂等性）
 * 2. 清零控制块并设置魔术字
 * 3. 初始化统计信息互斥锁
 * 4. 初始化所有事件类型条目的互斥锁
 * 5. 初始化消息队列
 * 6. 设置初始状态
 *
 * @return EVENT_OK 成功
 */
event_status_t event_system_init(void) {
    LOG_INF("Initializing event system...");

    if (g_event_system.initialized) {
        LOG_WRN("Event system already initialized");
        return EVENT_OK;
    }

    /* 初始化控制块 */
    memset(&g_event_system, 0, sizeof(g_event_system));
    g_event_system.magic = EVENT_SYSTEM_MAGIC;

    /* 初始化互斥锁 */
    k_mutex_init(&g_event_system.stats_lock);

    /* 初始化事件类型条目 */
    for (int i = 0; i < MAX_EVENT_TYPES; i++) {
        g_event_system.event_types[i].type = i;
        k_mutex_init(&g_event_system.event_types[i].lock);
    }

    /* 初始化消息队列 */
    k_msgq_init(&g_event_msgq, g_event_msgq_buffer, sizeof(event_t), CONFIG_EVENT_QUEUE_SIZE);
    g_event_system.event_queue = &g_event_msgq;

    g_event_system.next_subscriber_id = 1;
    atomic_set(&g_event_system.running, 0);
    g_event_system.initialized = true;

    LOG_INF("Event system initialized successfully");
    return EVENT_OK;
}

/**
 * @brief 启动事件系统
 *
 * 标记为运行状态；事件消费由 event_dispatcher 线程完成。
 *
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 未初始化
 */
event_status_t event_system_start(void) {
    if (!g_event_system.initialized) {
        LOG_ERR("Event system not initialized");
        return EVENT_ERR_INVALID_ARG;
    }

    if (atomic_get(&g_event_system.running) != 0) {
        LOG_WRN("Event system already running");
        return EVENT_OK;
    }

    atomic_set(&g_event_system.running, 1);
    LOG_INF("Event system started");
    return EVENT_OK;
}

/**
 * @brief 停止事件系统
 *
 * @return EVENT_OK 成功
 */
event_status_t event_system_stop(void) {
    if (!g_event_system.initialized) {
        return EVENT_OK;
    }

    if (atomic_get(&g_event_system.running) == 0) {
        return EVENT_OK;
    }

    atomic_set(&g_event_system.running, 0);

    purge_event_queue_and_free_payload(g_event_system.event_queue);

    LOG_INF("Event system stopped");
    return EVENT_OK;
}

/**
 * @brief 检查事件系统是否正在运行
 *
 * @return true 正在运行，false 已停止
 */
bool event_system_is_running(void) {
    return atomic_get(&g_event_system.running) != 0;
}

/* =============================================================================
 * 事件类型管理 (Event Type Management)
 * ============================================================================= */

/**
 * @brief 注册事件类型
 *
 * @param type 事件类型 ID
 * @param name 事件类型名称
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数
 */
event_status_t event_register_type(event_type_t type, const char* name) {
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES) {
        LOG_ERR("Invalid event type: %d", type);
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    k_mutex_lock(&entry->lock, K_FOREVER);

    if (entry->name != NULL) {
        k_mutex_unlock(&entry->lock);
        LOG_WRN("Event type %d already registered", type);
        return EVENT_OK; /* 幂等操作 */
    }

    entry->name = name;
    entry->subscriber_count = 0;
    memset(entry->subscribers, 0, sizeof(entry->subscribers));

    k_mutex_unlock(&entry->lock);

    LOG_DBG("Registered event type: %s (%d)", name, type);
    return EVENT_OK;
}

/**
 * @brief 注销事件类型
 *
 * @param type 事件类型 ID
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，
 *         EVENT_ERR_NOT_FOUND 未找到，EVENT_ERR_NO_SUBSCRIBER 仍有订阅者
 */
event_status_t event_unregister_type(event_type_t type) {
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    k_mutex_lock(&entry->lock, K_FOREVER);

    if (entry->name == NULL) {
        k_mutex_unlock(&entry->lock);
        return EVENT_ERR_NOT_FOUND;
    }

    /* 检查是否有活跃订阅者 */
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
 * 订阅管理 (Subscription Management)
 * ============================================================================= */

/**
 * @brief 订阅事件类型
 *
 * @param type 事件类型 ID
 * @param callback 回调函数指针
 * @param user_data 用户数据
 * @param subscriber_id 输出参数，接收分配的订阅者 ID
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_QUEUE_FULL 订阅者已满
 */
event_status_t event_subscribe(event_type_t type, event_callback_t callback, void* user_data, uint32_t* subscriber_id) {
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES || callback == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    k_mutex_lock(&entry->lock, K_FOREVER);

    if (entry->name == NULL) {
        k_mutex_unlock(&entry->lock);
        return EVENT_ERR_NOT_FOUND;
    }

    /* 查找空闲槽位 */
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
            LOG_DBG("Subscriber %d registered for event type %d", entry->subscribers[i].subscriber_id, type);
            return EVENT_OK;
        }
    }

    k_mutex_unlock(&entry->lock);
    LOG_ERR("No room for more subscribers on event type %d", type);
    return EVENT_ERR_QUEUE_FULL;
}

/**
 * @brief 取消订阅事件类型
 *
 * @param type 事件类型 ID
 * @param subscriber_id 订阅者 ID
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_NOT_FOUND 未找到
 */
event_status_t event_unsubscribe(event_type_t type, uint32_t subscriber_id) {
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (type >= MAX_EVENT_TYPES || subscriber_id == 0) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    k_mutex_lock(&entry->lock, K_FOREVER);

    subscriber_entry_t* sub = find_subscriber(entry, subscriber_id);
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

/**
 * @brief 从所有事件类型中取消订阅
 *
 * @param subscriber_id 订阅者 ID
 */
void event_unsubscribe_all(uint32_t subscriber_id) {
    if (!g_event_system.initialized || subscriber_id == 0) {
        return;
    }

    for (event_type_t type = 0; type < MAX_EVENT_TYPES; type++) {
        event_type_entry_t* entry = &g_event_system.event_types[type];

        if (entry->name == NULL) {
            continue; /* 跳过未注册的类型 */
        }

        k_mutex_lock(&entry->lock, K_FOREVER);

        subscriber_entry_t* sub = find_subscriber(entry, subscriber_id);
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
 * 事件发布 (Event Publishing)
 * ============================================================================= */

/**
 * @brief 发布事件（同步方式）
 *
 * @param event 要发布的事件
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_QUEUE_FULL 队列已满
 */
event_status_t event_publish(const event_t* event) {
    if (!g_event_system.initialized || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (atomic_get(&g_event_system.running) == 0) {
        LOG_WRN("Event system not running, event dropped");
        return EVENT_ERR_INVALID_ARG;
    }

    /* 验证事件类型 */
    if (event->type >= MAX_EVENT_TYPES || g_event_system.event_types[event->type].name == NULL) {
        LOG_WRN("Publishing to unregistered event type: %d", event->type);
    }

    int ret = k_msgq_put(g_event_system.event_queue, event, K_NO_WAIT);
    if (ret != 0) {
        atomic_fetch_add_explicit(&g_event_dropped_count, 1U, memory_order_relaxed);

        LOG_WRN("Event queue full, event dropped (type=%d)", event->type);
        return EVENT_ERR_QUEUE_FULL;
    }

    return EVENT_OK;
}

/**
 * @brief 从中断服务程序 (ISR) 发布事件
 *
 * @param event 要发布的事件
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_QUEUE_FULL 队列已满
 */
event_status_t event_publish_from_isr(const event_t* event) {
    if (!g_event_system.initialized || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (atomic_get(&g_event_system.running) == 0) {
        return EVENT_ERR_INVALID_ARG;
    }

    int ret = k_msgq_put(g_event_system.event_queue, event, K_NO_WAIT);
    if (ret != 0) {
        atomic_fetch_add_explicit(&g_event_dropped_count, 1U, memory_order_relaxed);
        return EVENT_ERR_QUEUE_FULL;
    }

    return EVENT_OK;
}

/**
 * @brief 发布事件并复制数据
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要复制的数据
 * @param data_len 数据长度
 * @return EVENT_OK 成功，EVENT_ERR_NO_MEM 内存不足
 */
event_status_t event_publish_copy(event_type_t type, event_priority_t priority, const void* data, size_t data_len) {
    event_t* event = event_create_with_data(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);
    /*
     * k_msgq_put 会复制 event_t；队列中的副本仍然指向堆上的 event->data。
     * 只释放事件外壳；数据负载由队列中的消息拥有。
     */
    if (status == EVENT_OK) {
        if (event->data != NULL && event->is_dynamic) {
            event->data = NULL;
            event->is_dynamic = false;
        }
        event_free(event);
    } else {
        event_free(event);
    }

    return status;
}

/* =============================================================================
 * 事件创建与内存管理 (Event Creation & Memory Management)
 * ============================================================================= */

/**
 * @brief 创建新事件
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @return 指向新事件的指针，失败返回 NULL
 */
event_t* event_create(event_type_t type, event_priority_t priority) {
    if (!g_event_system.initialized) {
        return NULL;
    }

    event_t* event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        LOG_ERR("Failed to allocate event");
        return NULL;
    }

    memset(event, 0, sizeof(event_t));
    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32(); /* 毫秒时间戳 */
    event->is_dynamic = true;

    return event;
}

/**
 * @brief 创建带数据的事件
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要附加的数据
 * @param data_len 数据长度
 * @return 指向新事件的指针，失败返回 NULL
 */
event_t* event_create_with_data(event_type_t type, event_priority_t priority, const void* data, size_t data_len) {
    if (data == NULL || data_len == 0) {
        return event_create(type, priority);
    }

    event_t* event = event_create(type, priority);
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

/**
 * @brief 释放事件对象
 *
 * @param event 要释放的事件
 */
void event_free(event_t* event) {
    if (event == NULL) {
        return;
    }

    if (event->is_dynamic && event->data != NULL) {
        k_free(event->data);
    }
    k_free(event);
}

/* =============================================================================
 * 工具函数 (Utility Functions)
 * ============================================================================= */

/**
 * @brief 获取事件类型名称
 *
 * @param type 事件类型 ID
 * @return 事件类型名称字符串
 */
const char* event_get_type_name(event_type_t type) {
    if (!g_event_system.initialized || type >= MAX_EVENT_TYPES) {
        return "UNKNOWN";
    }

    const char* name = g_event_system.event_types[type].name;
    return name != NULL ? name : "UNREGISTERED";
}

/**
 * @brief 获取事件类型的订阅者数量
 *
 * @param type 事件类型 ID
 * @return 活跃订阅者数量
 */
uint32_t event_get_subscriber_count(event_type_t type) {
    if (!g_event_system.initialized || type >= MAX_EVENT_TYPES) {
        return 0;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];
    k_mutex_lock(&entry->lock, K_FOREVER);
    uint32_t count = entry->subscriber_count;
    k_mutex_unlock(&entry->lock);

    return count;
}

/**
 * @brief 获取事件系统统计信息
 *
 * @param total_events 输出：已处理的事件总数
 * @param queue_depth 输出：当前队列深度
 * @param dropped_events 输出：被丢弃的事件数量
 */
void event_get_statistics(uint32_t* total_events, uint32_t* queue_depth, uint32_t* dropped_events) {
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
        *dropped_events = atomic_load_explicit(&g_event_dropped_count, memory_order_relaxed);
    }

    k_mutex_unlock(&g_event_system.stats_lock);
}

/* =============================================================================
 * 内部函数 (Internal Functions)
 * ============================================================================= */

/**
 * @brief 将事件分发给所有订阅者
 *
 * 使用快照技术：先复制所有活跃订阅者的回调信息，
 * 然后在锁外执行回调，避免死锁和竞态条件。
 *
 * @param event 要分发的事件
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，
 *         EVENT_ERR_NO_SUBSCRIBER 无订阅者
 */
event_status_t event_notify_subscribers(const event_t* event) {
    if (event == NULL || event->type >= MAX_EVENT_TYPES) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[event->type];

    /* 快照结构：保存订阅者的回调和用户数据 */
    typedef struct {
        event_callback_t cb;
        void*            ud;
    } sub_snap_t;

    sub_snap_t snap[CONFIG_EVENT_MAX_SUBSCRIBERS];
    uint32_t   n = 0U;

    k_mutex_lock(&entry->lock, K_FOREVER);

    if (entry->subscriber_count == 0) {
        k_mutex_unlock(&entry->lock);
        k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
        g_event_system.total_events++;
        k_mutex_unlock(&g_event_system.stats_lock);
        return EVENT_ERR_NO_SUBSCRIBER;
    }

    /* 复制活跃订阅者信息到快照 */
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        subscriber_entry_t* sub = &entry->subscribers[i];

        if (sub->is_active && sub->callback != NULL) {
            snap[n].cb = sub->callback;
            snap[n].ud = sub->user_data;
            n++;
        }
    }

    k_mutex_unlock(&entry->lock);

    /* 在锁外调用所有回调 */
    for (uint32_t i = 0; i < n; i++) {
        snap[i].cb(event, snap[i].ud);
    }

    k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
    g_event_system.total_events++;
    k_mutex_unlock(&g_event_system.stats_lock);

    return EVENT_OK;
}

/**
 * @brief 获取全局事件队列指针
 *
 * @return 指向全局事件队列的指针，未初始化时返回 NULL
 */
struct k_msgq* event_system_get_queue(void) {
    if (!g_event_system.initialized) {
        return NULL;
    }
    return g_event_system.event_queue;
}

/**
 * @brief 查找订阅者
 *
 * @param entry 事件类型条目
 * @param subscriber_id 订阅者 ID
 * @return 指向订阅者条目的指针，未找到返回 NULL
 */
static subscriber_entry_t* find_subscriber(event_type_entry_t* entry, uint32_t subscriber_id) {
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        if (entry->subscribers[i].is_active && entry->subscribers[i].subscriber_id == subscriber_id) {
            return &entry->subscribers[i];
        }
    }
    return NULL;
}

static void purge_event_queue_and_free_payload(struct k_msgq* queue) {
    if (queue == NULL) {
        return;
    }

    event_t ev;

    while (k_msgq_get(queue, &ev, K_NO_WAIT) == 0) {
        if (ev.is_dynamic && ev.data != NULL) {
            k_free(ev.data);
            ev.data = NULL;
        }
    }
}
