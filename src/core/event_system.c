/**
 * @file event_system.c
 * @brief 核心事件系统实现
 *
 * 基于发布 - 订阅模式的线程安全高性能事件系统。
 *
 * 架构说明：
 * - 事件队列：使用 Zephyr k_msgq 实现，支持多生产者单消费者
 * - 事件分发：由 event_dispatcher 模块中的线程消费队列并调用 event_notify_subscribers
 * - 订阅管理：每个事件类型维护一个订阅者列表
 * - 线程安全：使用互斥锁保护共享数据结构
 * - ISR 支持：提供专门的中断上下文发布函数
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */
#include "event_system.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <string.h>
#include "event_dispatcher.h"
#include "event_memory.h"
#include "event_queue.h"
#include "lock_order.h"
#include "state_machine.h"

LOG_MODULE_REGISTER(event_system, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义
 * ============================================================================= */

/** 最大支持的事件类型数量（从 Kconfig 获取） */
#define MAX_EVENT_TYPES         CONFIG_EVENT_MAX_TYPES
#define MAX_EVENT_TYPE_ID       (MAX_EVENT_TYPES - 1U)

/** 魔术字，用于验证控制块有效性 ("EVNT") */
#define EVENT_SYSTEM_MAGIC      0x45564E54

/** 未初始化或已完成 shutdown（与 BSS 初值一致），非损坏状态 */
#define EVENT_SYSTEM_MAGIC_IDLE 0U

/** SIL-2: 验证事件系统魔术字（返回 event_status_t 版本） */
#define EVENT_SYSTEM_VALIDATE()                                                                                        \
    do {                                                                                                               \
        if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {                                                         \
            return EVENT_ERR_INVALID_ARG;                                                                              \
        }                                                                                                              \
        if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {                                                              \
            LOG_ERR("Event system magic corruption detected: 0x%08x", g_event_system.magic);                           \
            return EVENT_ERR_INVALID_ARG;                                                                              \
        }                                                                                                              \
    } while (0)

/** SIL-2: 验证事件系统魔术字（返回 void 版本） */
#define EVENT_SYSTEM_VALIDATE_VOID()                                                                                   \
    do {                                                                                                               \
        if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {                                                              \
            LOG_ERR("Event system magic corruption detected: 0x%08x", g_event_system.magic);                           \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

/** 分配类 API：空闲态静默失败，非法 magic 记录损坏 */
#define EVENT_SYSTEM_CHECK_MAGIC_ALLOC()                                                                               \
    do {                                                                                                               \
        if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {                                                         \
            return NULL;                                                                                               \
        }                                                                                                              \
        if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {                                                              \
            LOG_ERR("Event system magic corruption detected");                                                         \
            return NULL;                                                                                               \
        }                                                                                                              \
    } while (0)

/** 释放类 API：空闲态静默返回 */
#define EVENT_SYSTEM_CHECK_MAGIC_FREE_VOID()                                                                           \
    do {                                                                                                               \
        if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {                                                              \
            LOG_ERR("Event system magic corruption detected");                                                         \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

/** 初始化保护标志 */
static atomic_t g_init_lock = ATOMIC_INIT(0);

/** stop 期间停止了分发器；start 时需重新 event_dispatcher_start()（原子位，避免 start/stop 并发撕裂） */
static atomic_t g_restart_dispatcher_on_start;

#if defined(CONFIG_EVENT_QUEUE_OVERFLOW_DROP_LOWEST)
#define EVENT_PUBLISH_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_DROP_LOWEST
#define EVENT_PUBLISH_ISR_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_DROP_NEWEST
#elif defined(CONFIG_EVENT_QUEUE_OVERFLOW_BLOCK)
#define EVENT_PUBLISH_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_BLOCK
#define EVENT_PUBLISH_ISR_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_BLOCK
#else
#define EVENT_PUBLISH_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_DROP_NEWEST
#define EVENT_PUBLISH_ISR_QUEUE_OVERFLOW_POLICY QUEUE_OVERFLOW_DROP_NEWEST
#endif

#if defined(CONFIG_EVENT_QUEUE_OVERFLOW_BLOCK)
/** BLOCK 策略下 publish 入队应阻塞等待空位，不可使用 K_NO_WAIT */
#define EVENT_PUBLISH_ENQUEUE_TIMEOUT K_FOREVER
#else
#define EVENT_PUBLISH_ENQUEUE_TIMEOUT K_NO_WAIT
#endif

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

/**
 * @brief 事件系统控制块
 *
 * 包含事件系统的全局状态和数据结构。
 */
typedef struct {
    uint32_t             magic;                        /**< 魔术字，用于验证有效性 */
    bool                 initialized;                  /**< 系统是否已初始化 */
    atomic_t             running;                      /**< 非 0：允许投递（线程与 ISR 可读） */
    zepl_state_machine_t lifecycle;                    /**< init/start/stop/shutdown 生命周期状态机 */
    struct k_msgq*       event_queue;                  /**< 事件队列指针 */
    event_type_entry_t   event_types[MAX_EVENT_TYPES]; /**< 事件类型表 */
    uint32_t             total_events;                 /**< 已处理的事件总数 */
    struct k_mutex       stats_lock;                   /**< 保护统计信息的互斥锁 */
    atomic_t             next_subscriber_id;           /**< 下一个可用的订阅者 ID (原子操作保护) */
} event_system_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

/** 全局事件系统控制块实例 */
static event_system_cb_t g_event_system;

/** 全局事件消息队列 */
static struct k_msgq g_event_msgq;

/** 事件消息队列缓冲区 */
static char g_event_msgq_buffer[CONFIG_EVENT_QUEUE_SIZE * sizeof(event_t)] __aligned(__alignof__(event_t));

/**
 * ISR 安全的丢弃计数器
 * 使用 Zephyr atomic_t 避免在 event_publish_from_isr 中使用互斥锁
 */
static atomic_t g_event_dropped_count;

/**
 * 正在执行 event_publish / event_publish_from_isr 入队路径的调用数（ISR 安全）。
 * stop/shutdown 在 purge/deinit 前等待其归零，避免 running 检查后仍向队列投递。
 */
static atomic_t g_publish_in_flight;
/** stop/shutdown 等待 in-flight publish 归零的最大等待时间（毫秒） */
#define EVENT_PUBLISH_IN_FLIGHT_WAIT_TIMEOUT_MS 5000U

/** 串行化订阅者 ID 分配与全局唯一性检查，消除 subscriber_id_in_use 的 TOCTOU */
static K_MUTEX_DEFINE(g_subscriber_id_lock);

static void event_system_lifecycle_lock_wait(void) {
    while (atomic_test_and_set_bit(&g_init_lock, 0)) {
        k_yield();
    }
}

static bool event_system_lifecycle_try_lock(void) {
    return !atomic_test_and_set_bit(&g_init_lock, 0);
}

static void event_system_lifecycle_unlock(void) {
    atomic_clear_bit(&g_init_lock, 0);
}

static zepl_state_t event_system_lifecycle_state(void) {
    return zepl_state_machine_get(&g_event_system.lifecycle);
}

static void event_system_stats_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_GLOBAL, (uintptr_t) &g_event_system.stats_lock);
    k_mutex_lock(&g_event_system.stats_lock, K_FOREVER);
}

static void event_system_stats_unlock(void) {
    k_mutex_unlock(&g_event_system.stats_lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_GLOBAL, (uintptr_t) &g_event_system.stats_lock);
}

static void event_system_subscriber_id_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_TABLE, (uintptr_t) &g_subscriber_id_lock);
    k_mutex_lock(&g_subscriber_id_lock, K_FOREVER);
}

static void event_system_subscriber_id_unlock(void) {
    k_mutex_unlock(&g_subscriber_id_lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_TABLE, (uintptr_t) &g_subscriber_id_lock);
}

static void event_system_entry_lock(event_type_entry_t* entry) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_ENTRY, (uintptr_t) &entry->lock);
    k_mutex_lock(&entry->lock, K_FOREVER);
}

static void event_system_entry_unlock(event_type_entry_t* entry) {
    k_mutex_unlock(&entry->lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_ENTRY, (uintptr_t) &entry->lock);
}

static void event_publish_in_flight_wait_zero(void) {
    int64_t deadline = k_uptime_get() + (int64_t) EVENT_PUBLISH_IN_FLIGHT_WAIT_TIMEOUT_MS;
    while (atomic_get(&g_publish_in_flight) != 0) {
        if (k_uptime_get() >= deadline) {
            LOG_ERR("Timeout waiting publish in-flight to drain: %d", (int) atomic_get(&g_publish_in_flight));
            break;
        }
        k_sleep(K_MSEC(1));
    }
}

/**
 * @brief 校验待发布事件的 flags / data_len / 指针一致性
 */
static event_status_t event_validate_for_publish(const event_t* event) {
    if (event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    const uint32_t len = event->data_len;
    const uint8_t  flags = event->flags;
    const uint8_t  storage =
        flags & (EVENT_FLAG_DATA_INLINE | EVENT_FLAG_DATA_DYNAMIC | EVENT_FLAG_DATA_FROM_SLAB | EVENT_FLAG_SLAB_MASK);

    if (len == 0U) {
        if (storage != 0U) {
            LOG_WRN("publish: data_len=0 but storage flags 0x%02x", storage);
            return EVENT_ERR_INVALID_ARG;
        }
        return EVENT_OK;
    }

    if ((flags & EVENT_FLAG_DATA_INLINE) && (flags & EVENT_FLAG_DATA_DYNAMIC)) {
        LOG_WRN("publish: both INLINE and DYNAMIC flags set");
        return EVENT_ERR_INVALID_ARG;
    }

    if (flags & EVENT_FLAG_DATA_INLINE) {
        if (len > CONFIG_EVENT_INLINE_DATA_SIZE) {
            LOG_WRN("publish: INLINE data_len %u exceeds %u", len, CONFIG_EVENT_INLINE_DATA_SIZE);
            return EVENT_ERR_INVALID_ARG;
        }
        return EVENT_OK;
    }

    if (flags & EVENT_FLAG_DATA_DYNAMIC) {
        if (event->data.ptr == NULL) {
            LOG_WRN("publish: DYNAMIC with NULL data.ptr");
            return EVENT_ERR_INVALID_ARG;
        }
        if ((flags & EVENT_FLAG_DATA_FROM_SLAB) && ((flags & EVENT_FLAG_SLAB_MASK) == 0U)) {
            LOG_WRN("publish: DATA_FROM_SLAB without SLAB_MASK");
            return EVENT_ERR_INVALID_ARG;
        }
        return EVENT_OK;
    }

    LOG_WRN("publish: data_len=%u without INLINE/DYNAMIC flags", len);
    return EVENT_ERR_INVALID_ARG;
}

static bool event_type_is_registered(event_type_t type) {
    if ((uint32_t) type > MAX_EVENT_TYPE_ID) {
        return false;
    }

    return atomic_get(&g_event_system.event_types[type].registered) != 0;
}

#if EVENT_SLAB_ENABLED
static const char* event_slab_name_for_priority(event_priority_t priority) {
    switch (priority) {
#if EVENT_SLAB_CRITICAL_AVAILABLE
    case EVENT_PRIORITY_CRITICAL:
        return "event_slab_critical";
#endif
#if EVENT_SLAB_HIGH_AVAILABLE
    case EVENT_PRIORITY_HIGH:
        return "event_slab_high";
#endif
    case EVENT_PRIORITY_NORMAL:
    case EVENT_PRIORITY_LOW:
    default:
        return "event_slab_normal";
    }
}
#endif

/**
 * @brief 入队成功后，调用方 event 不再拥有动态负载（队列副本持有）
 */
static void event_publish_transfer_data_ownership(event_t* event) {
    if (event == NULL) {
        return;
    }
    event->flags &= ~(EVENT_FLAG_DATA_DYNAMIC | EVENT_FLAG_DATA_FROM_SLAB | EVENT_FLAG_SLAB_MASK);
}

static event_status_t event_publish_common(event_t* event, queue_overflow_policy_t policy, k_timeout_t timeout,
                                           bool log_failures) {
    event_status_t status = EVENT_OK;

    EVENT_SYSTEM_VALIDATE();
    if (!g_event_system.initialized || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    if (atomic_get(&g_event_system.running) == 0) {
#ifndef CONFIG_EVENT_SYSTEM_LOG_MINIMAL
        if (log_failures) {
            LOG_WRN("Event system not running, event dropped");
        }
#endif
        return EVENT_ERR_NOT_RUNNING;
    }

    (void) atomic_inc(&g_publish_in_flight);

    if (atomic_get(&g_event_system.running) == 0) {
        status = EVENT_ERR_NOT_RUNNING;
#ifndef CONFIG_EVENT_SYSTEM_LOG_MINIMAL
        if (log_failures) {
            LOG_WRN("Event system not running, event dropped");
        }
#endif
        goto out;
    }

    if ((uint32_t) event->type > MAX_EVENT_TYPE_ID) {
        status = EVENT_ERR_INVALID_ARG;
#ifndef CONFIG_EVENT_SYSTEM_LOG_MINIMAL
        if (log_failures) {
            LOG_WRN("Invalid event type id %u (max %u)", (unsigned int) event->type, (unsigned int) MAX_EVENT_TYPE_ID);
        }
#endif
        goto out;
    }

    status = event_validate_for_publish(event);
    if (status != EVENT_OK) {
        goto out;
    }

    if (!event_type_is_registered(event->type)) {
        status = EVENT_ERR_NOT_FOUND;
#ifndef CONFIG_EVENT_SYSTEM_LOG_MINIMAL
        if (log_failures) {
            LOG_WRN("Publishing to unregistered event type: %d", event->type);
        }
#endif
        goto out;
    }

    status = event_queue_enqueue(g_event_system.event_queue, event, policy, timeout);
    if (status == EVENT_OK) {
        event_publish_transfer_data_ownership(event);
    }

out:
    atomic_dec(&g_publish_in_flight);
    return status;
}

/** 由 event_queue 溢出路径同步更新分发器统计（前向声明，避免与 dispatcher 头文件循环包含） */
extern void event_dispatcher_stats_inc_dropped(void);

/**
 * @brief 增加全局丢弃计数器（内部接口，供 event_queue.c 调用）
 */
void event_system_inc_dropped_count(void) {
    atomic_inc(&g_event_dropped_count);
    event_dispatcher_stats_inc_dropped();
}

/* =============================================================================
 * 前置声明与内部辅助 (Forward Declarations & Internal Helpers)
 * ============================================================================= */

/**
 * @brief 查找订阅者
 * @param entry 事件类型条目
 * @param subscriber_id 订阅者 ID
 * @return 指向订阅者条目的指针，未找到返回 NULL
 */
static subscriber_entry_t* find_subscriber(event_type_entry_t* entry, uint32_t subscriber_id);

#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
/**
 * @brief 从 slab 分配数据并绑定到事件（标记失败时回滚 slab 分配）
 */
static bool event_attach_slab_data(event_t* event, struct k_mem_slab* slab, const void* data, size_t data_len) {
    if (event == NULL || slab == NULL || data == NULL) {
        return false;
    }

    if (k_mem_slab_alloc(slab, &event->data.ptr, K_NO_WAIT) != 0) {
        return false;
    }

    event->flags |= EVENT_FLAG_DATA_DYNAMIC | EVENT_FLAG_DATA_FROM_SLAB;
    if (!event_memory_data_slab_set_flag(event, slab)) {
        k_mem_slab_free(slab, event->data.ptr);
        event->data.ptr = NULL;
        event->flags &= ~(EVENT_FLAG_DATA_DYNAMIC | EVENT_FLAG_DATA_FROM_SLAB | EVENT_FLAG_SLAB_MASK);
        return false;
    }

    memcpy(event->data.ptr, data, data_len);
    event->data_len = (uint32_t) data_len;
    event_debug_track_alloc(event->data.ptr, data_len, event->priority);
    return true;
}
#endif /* EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE */

/**
 * @brief 清理所有事件类型条目
 *
 * 释放所有事件类型的订阅者和相关资源。
 */
static void event_system_cleanup_event_types(void) {
    /* SIL-2: 无条件清理所有事件类型条目（不以 name!=NULL 为条件），
     * 避免 name 异常为 NULL 时订阅者数组不被清理 */
    for (int type = 0; type < MAX_EVENT_TYPES; type++) {
        event_type_entry_t* entry = &g_event_system.event_types[type];
        event_system_entry_lock(entry);
        atomic_set(&entry->registered, 0);
        entry->name = NULL;
        entry->name_storage[0] = '\0';
        entry->subscriber_count = 0;
        memset(entry->subscribers, 0, sizeof(entry->subscribers));
        event_system_entry_unlock(entry);
    }
}

/**
 * @brief 重置事件系统控制块
 *
 * 将控制块重置为初始状态。
 */
static void event_system_reset_control_block(void) {
    /* SIL-2: 重置控制块 */
    (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_UNINIT);
    g_event_system.initialized = false;
    g_event_system.total_events = 0;
    atomic_set(&g_event_system.next_subscriber_id, 1);
    g_event_system.event_queue = NULL;
    /* 与未初始化态一致，避免将 shutdown 误判为 magic 损坏 */
    g_event_system.magic = EVENT_SYSTEM_MAGIC_IDLE;
}

/**
 * @brief 检查订阅者 ID 是否已被使用
 *
 * @note 调用方必须已持有 g_subscriber_id_lock（串行化所有 subscribe/unsubscribe）。
 *       遍历时会逐类型短暂持有 entry->lock；调用方不得已持有任何 entry->lock，
 *       以保持 TABLE -> ENTRY 且同层 entry 按表顺序获取的锁序。

 */
static bool subscriber_id_in_use(uint32_t id) {
    for (int t = 0; t < MAX_EVENT_TYPES; t++) {
        event_type_entry_t* entry = &g_event_system.event_types[t];
        event_system_entry_lock(entry);
        for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
            if (entry->subscribers[i].is_active && entry->subscribers[i].subscriber_id == id) {
                event_system_entry_unlock(entry);
                return true;
            }
        }
        event_system_entry_unlock(entry);
    }
    return false;
}

/* =============================================================================
 * 核心实现 (Core Implementation)
 * ============================================================================= */

/**
 * @brief 回滚未完成的 event_system_init（内部）
 *
 * 在设置 magic / initialized 之前失败时调用，保证重试可安全 memset + k_mutex_init。
 */
static void event_system_init_rollback(void) {
    event_queue_deinit(&g_event_msgq);
    memset(&g_event_system, 0, sizeof(g_event_system));
    atomic_set(&g_publish_in_flight, 0);
    atomic_clear(&g_restart_dispatcher_on_start);
}

/**
 * @brief 初始化事件系统
 *
 * 初始化步骤：
 * 1. 检查是否已初始化（幂等性）
 * 2. 清零控制块（magic 在全部步骤成功后再设置）
 * 3. 初始化统计信息互斥锁
 * 4. 初始化所有事件类型条目的互斥锁
 * 5. 初始化消息队列
 * 6. 设置魔术字与 initial 状态
 *
 * @return EVENT_OK 成功
 */
event_status_t event_system_init(void) {
    LOG_DBG("Initializing event system...");

    /* SIL-2: 使用原子标志保护初始化，防止多线程竞争 */
    if (event_dispatcher_is_current_thread()) {
        if (!event_system_lifecycle_try_lock()) {
            LOG_ERR("Cannot init event system from dispatcher thread during lifecycle transition");
            return EVENT_ERR_INVALID_ARG;
        }
    } else {
        event_system_lifecycle_lock_wait();
    }

    if (g_event_system.initialized) {
        if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
            LOG_ERR("Event system initialized flag set but magic is invalid: 0x%08x", g_event_system.magic);
            event_system_lifecycle_unlock();
            return EVENT_ERR_INVALID_ARG;
        }
        event_system_lifecycle_unlock();
        LOG_WRN("Event system already initialized");
        return EVENT_OK;
    }

    if (g_event_system.magic != EVENT_SYSTEM_MAGIC_IDLE) {
        LOG_ERR("Event system magic corruption detected before init: 0x%08x", g_event_system.magic);
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }

    /* 初始化控制块（成功前不设置 magic，失败后可完整重试） */
    memset(&g_event_system, 0, sizeof(g_event_system));
    atomic_set(&g_publish_in_flight, 0);
    zepl_state_machine_init(&g_event_system.lifecycle, ZEP_STATE_UNINIT);

    /* 初始化互斥锁 */
    k_mutex_init(&g_event_system.stats_lock);

    /* 初始化事件类型条目 */
    for (int i = 0; i < MAX_EVENT_TYPES; i++) {
        g_event_system.event_types[i].type = i;
        k_mutex_init(&g_event_system.event_types[i].lock);
    }

    g_event_system.event_queue = &g_event_msgq;
    atomic_clear(&g_restart_dispatcher_on_start);

    /* 注册队列到 event_queue 管理层（内部 k_msgq_init + 溢出策略统计） */
    event_status_t qret = event_queue_init(&g_event_msgq, g_event_msgq_buffer, CONFIG_EVENT_QUEUE_SIZE);
    if (qret != EVENT_OK) {
        event_system_init_rollback();
        event_system_lifecycle_unlock();
        LOG_ERR("event_queue_init failed: %d", qret);
        return qret;
    }

    g_event_system.magic = EVENT_SYSTEM_MAGIC;
    atomic_set(&g_event_system.next_subscriber_id, 1);
    atomic_set(&g_event_system.running, 0);
    g_event_system.initialized = true;
    if (zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_INITED) != 0) {
        event_system_init_rollback();
        event_system_lifecycle_unlock();
        LOG_ERR("Failed to transition event system to INITED");
        return EVENT_ERR_INVALID_ARG;
    }

    event_system_lifecycle_unlock();
    LOG_DBG("Event system initialized successfully");
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
    if (!event_system_lifecycle_try_lock()) {
        LOG_WRN("Event system lifecycle transition in progress; start rejected");
        return EVENT_ERR_TIMEOUT;
    }

    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        LOG_ERR("Event system magic corruption detected: 0x%08x", g_event_system.magic);
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }
    if (!g_event_system.initialized) {
        LOG_ERR("Event system not initialized");
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }

    {
        const zepl_state_t lc_state = event_system_lifecycle_state();

        if (lc_state == ZEP_STATE_RUNNING && atomic_get(&g_event_system.running) != 0) {
            LOG_WRN("Event system already running");
            event_system_lifecycle_unlock();
            return EVENT_OK;
        }

        if (lc_state != ZEP_STATE_INITED && lc_state != ZEP_STATE_STOPPED) {
            LOG_ERR("Event system not in a startable state: %s", zepl_state_name(lc_state));
            event_system_lifecycle_unlock();
            return EVENT_ERR_INVALID_ARG;
        }

        if (zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STARTING) != 0) {
            LOG_ERR("Failed to transition event system to STARTING from %s", zepl_state_name(lc_state));
            event_system_lifecycle_unlock();
            return EVENT_ERR_INVALID_ARG;
        }

        if (zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_RUNNING) != 0) {
            /* STARTING cannot transition directly to INITED/STOPPED; use the valid stop path. */
            (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPING);
            (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPED);
            LOG_ERR("Failed to transition event system to RUNNING from STARTING; rolled back to STOPPED");
            event_system_lifecycle_unlock();
            return EVENT_ERR_INVALID_ARG;
        }
    }

    if (atomic_cas(&g_restart_dispatcher_on_start, 1, 0)) {
        if (event_dispatcher_is_initialized()) {
            event_status_t dret = event_dispatcher_start();
            if (dret != EVENT_OK) {
                LOG_ERR("event_dispatcher_start during event_system_start failed: %d", dret);
                atomic_set(&g_restart_dispatcher_on_start, 1);
                (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPING);
                (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPED);
                event_system_lifecycle_unlock();
                return dret;
            }
        }
    }

    atomic_set(&g_event_system.running, 1);
    event_system_lifecycle_unlock();
    LOG_DBG("Event system started");
    return EVENT_OK;
}

/**
 * @brief 停止事件系统
 *
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_system_stop(void) {
    if (event_dispatcher_is_current_thread()) {
        LOG_ERR("Cannot stop event system from dispatcher thread");
        return EVENT_ERR_INVALID_ARG;
    }

    event_system_lifecycle_lock_wait();

    /* 与 shutdown 一致：空闲态下幂等返回 OK，避免 VALIDATE 将 IDLE 误判为非法 */
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE || !g_event_system.initialized) {
        event_system_lifecycle_unlock();
        return EVENT_OK;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        LOG_ERR("Event system magic corruption detected: 0x%08x", g_event_system.magic);
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }

    if (atomic_get(&g_event_system.running) == 0) {
        /* If a previous stop failed after clearing running, this remains idempotent.
         * API docs call out that
         * dispatcher termination is not guaranteed in that fault state.
         */
        event_system_lifecycle_unlock();
        return EVENT_OK;
    }

    {
        const zepl_state_t lc_state = event_system_lifecycle_state();

        if (lc_state == ZEP_STATE_RUNNING &&
            zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPING) != 0) {
            LOG_ERR("Failed to transition event system to STOPPING");
            event_system_lifecycle_unlock();
            return EVENT_ERR_INVALID_ARG;
        }
    }

    atomic_set(&g_event_system.running, 0);

    /* 等待进行中的 publish 完成后再停分发器 / purge，避免 stop 返回后仍有事件入队 */
    event_publish_in_flight_wait_zero();

    {
        if (event_dispatcher_is_initialized()) {
            dispatcher_state_t disp_st = event_dispatcher_get_state();
            if (disp_st == DISPATCHER_RUNNING || disp_st == DISPATCHER_PAUSED) {
                atomic_set(&g_restart_dispatcher_on_start, 1);
                event_status_t dret = event_dispatcher_stop();
                if (dret != EVENT_OK) {
                    LOG_WRN("event_dispatcher_stop during event_system_stop: %d", dret);
                    (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_ERROR);
                    event_system_lifecycle_unlock();
                    return dret;
                }
            }
        }
    }

    event_queue_purge(g_event_system.event_queue);

    (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_STOPPED);

    event_system_lifecycle_unlock();
    LOG_DBG("Event system stopped");
    return EVENT_OK;
}

/**
 * @brief 关闭事件系统
 *
 * 完全关闭事件系统，清理所有资源，重置所有状态。
 *
 * @return EVENT_OK 成功
 *
 * @note MED-7: 不要在持有任何 subscriber callback 可能也需要获取的锁时调用此函数，
 *       否则可能因分发器线程正在执行该 callback 而产生死锁。
 */
event_status_t event_system_shutdown(void) {
    /* HIGH-NEW-2: 禁止从分发器线程内部调用 shutdown。
     * 该检查必须在生命周期锁之前执行：若外部线程已在 shutdown 并等待 join，
     * 分发器回调再阻塞等同一把锁会造成 join 超时。 */
    if (event_dispatcher_is_current_thread()) {
        LOG_ERR("Cannot shutdown event system from dispatcher thread");
        return EVENT_ERR_INVALID_ARG;
    }

    LOG_DBG("Shutting down event system...");

    /* SIL-2: 使用 g_init_lock 防止与 init/shutdown 并发执行（NEW-1）。
     * 与 event_system_init 共用同一原子标志，串行化整个生命周期变更。
     * 必须先拿锁再判断 initialized/magic，否则 shutdown 可能在 init 半途早退。 */
    event_system_lifecycle_lock_wait();

    /* 幂等：已完成 shutdown 后 magic 为 IDLE，勿再走 VALIDATE（否则会误报 INVALID_ARG） */
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE || !g_event_system.initialized) {
        event_system_lifecycle_unlock();
        return EVENT_OK;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        LOG_ERR("Event system magic corruption detected: 0x%08x", g_event_system.magic);
        event_system_lifecycle_unlock();
        return EVENT_ERR_INVALID_ARG;
    }

    if (!g_event_system.initialized) {
        event_system_lifecycle_unlock();
        return EVENT_OK;
    }

    /* SIL-2: 先停止运行状态，防止新事件入队 */
    atomic_set(&g_event_system.running, 0);

    /* 等待进行中的 publish 完成后再停 dispatcher / deinit 队列 */
    event_publish_in_flight_wait_zero();

    /* SIL-2: 主动停止 dispatcher 而非仅做防御性检查（HIGH-1）。
     * event_dispatcher_stop 内部 join 线程，返回后线程已退出，
     * 之后释放队列资源不会与悬挂线程产生竞态。 */
    if (event_dispatcher_is_initialized()) {
        event_status_t dret = event_dispatcher_deinit();
        if (dret != EVENT_OK) {
            LOG_ERR("Failed to deinit dispatcher during shutdown: %d", dret);
            (void) zepl_state_machine_try_transition(&g_event_system.lifecycle, ZEP_STATE_ERROR);
            /* running 保持为 0，禁止继续入队；不释放资源，便于上层重试 shutdown。
             *
             * EVENT_ERR_TIMEOUT：dispatcher join 超时，线程可能仍存活；宜记录故障并系统复位，
 *
             * 不宜在无人工介入下反复 shutdown。 */
            event_system_lifecycle_unlock();
            return dret;
        }
    }

    /* SIL-2: 反初始化事件队列，释放动态负载和 DROP_LOWEST scratch 缓冲区 */
    event_queue_deinit(g_event_system.event_queue);

    /* 清理所有事件类型条目 */
    event_system_cleanup_event_types();

    /* 重置控制块（event_queue_deinit 已 purge 队列，无需再次 k_msgq_purge） */
    event_system_reset_control_block();
    atomic_clear(&g_restart_dispatcher_on_start);

    event_system_lifecycle_unlock();
    LOG_DBG("Event system shutdown complete");
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
 * 事件类型管理
 * ============================================================================= */

/**
 * @brief 注册事件类型
 *
 * @param type 事件类型 ID
 * @param name 事件类型名称
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数
 */
event_status_t event_register_type(event_type_t type, const char* name) {
    EVENT_SYSTEM_VALIDATE();
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if ((uint32_t) type > MAX_EVENT_TYPE_ID) {
        LOG_ERR("Invalid event type: %d", type);
        return EVENT_ERR_INVALID_ARG;
    }

    if (name == NULL) {
        LOG_ERR("event_register_type: name cannot be NULL");
        return EVENT_ERR_INVALID_ARG;
    }

    {
        size_t name_len = strlen(name);
        if (name_len == 0U || name_len >= CONFIG_EVENT_TYPE_NAME_MAX) {
            LOG_ERR("Event type name invalid length %zu (max %d)", name_len, CONFIG_EVENT_TYPE_NAME_MAX - 1);
            return EVENT_ERR_INVALID_ARG;
        }
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    event_system_entry_lock(entry);

    if (entry->name != NULL) {
        event_system_entry_unlock(entry);
        LOG_WRN("Event type %d already registered", type);
        return EVENT_OK; /* 幂等操作 */
    }

    (void) strncpy(entry->name_storage, name, sizeof(entry->name_storage) - 1U);
    entry->name_storage[sizeof(entry->name_storage) - 1U] = '\0';
    entry->name = entry->name_storage;
    atomic_set(&entry->registered, 1);
    entry->subscriber_count = 0;
    memset(entry->subscribers, 0, sizeof(entry->subscribers));

    event_system_entry_unlock(entry);

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
    EVENT_SYSTEM_VALIDATE();
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if ((uint32_t) type > MAX_EVENT_TYPE_ID) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    event_system_entry_lock(entry);

    if (entry->name == NULL) {
        event_system_entry_unlock(entry);
        return EVENT_ERR_NOT_FOUND;
    }

    /* 检查是否有活跃订阅者 */
    if (entry->subscriber_count > 0) {
        event_system_entry_unlock(entry);
        LOG_WRN("Cannot unregister type %d with active subscribers", type);
        return EVENT_ERR_NO_SUBSCRIBER;
    }

    atomic_set(&entry->registered, 0);
    entry->name = NULL;
    entry->name_storage[0] = '\0';
    entry->subscriber_count = 0;

    event_system_entry_unlock(entry);

    LOG_DBG("Unregistered event type: %d", type);
    return EVENT_OK;
}

/* =============================================================================
 * 订阅管理
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
    EVENT_SYSTEM_VALIDATE();
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if ((uint32_t) type > MAX_EVENT_TYPE_ID || callback == NULL || subscriber_id == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    /* 固定锁顺序：g_subscriber_id_lock → entry->lock，与 unsubscribe 一致 */
    event_system_subscriber_id_lock();

    uint32_t new_id;
    uint32_t attempts = 0;
    while (true) {
        new_id = (uint32_t) atomic_inc(&g_event_system.next_subscriber_id);
        if (++attempts > UINT16_MAX) {
            LOG_ERR("Subscriber ID space exhausted after %u attempts", attempts);
            event_system_subscriber_id_unlock();
            return EVENT_ERR_NO_MEM;
        }
        if (new_id == EVENT_SUBSCRIBER_ID_INVALID) {
            continue;
        }
        if (!subscriber_id_in_use(new_id)) {
            break;
        }
    }

    event_system_entry_lock(entry);
    if (entry->name == NULL) {
        event_system_entry_unlock(entry);
        event_system_subscriber_id_unlock();
        return EVENT_ERR_NOT_FOUND;
    }

    uint32_t free_slot = CONFIG_EVENT_MAX_SUBSCRIBERS;
    for (uint32_t i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        if (!entry->subscribers[i].is_active) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == CONFIG_EVENT_MAX_SUBSCRIBERS) {
        event_system_entry_unlock(entry);
        event_system_subscriber_id_unlock();
        LOG_ERR("No room for more subscribers on event type %d", type);
        return EVENT_ERR_QUEUE_FULL;
    }

    entry->subscribers[free_slot].callback = callback;
    entry->subscribers[free_slot].user_data = user_data;
    entry->subscribers[free_slot].subscriber_id = new_id;
    entry->subscribers[free_slot].is_active = true;
    entry->subscriber_count++;
    *subscriber_id = new_id;

    event_system_entry_unlock(entry);
    event_system_subscriber_id_unlock();
    LOG_DBG("Subscriber %d registered for event type %d", new_id, type);
    return EVENT_OK;
}

/**
 * @brief 取消订阅事件类型
 *
 * @param type 事件类型 ID
 * @param subscriber_id 订阅者 ID
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_NOT_FOUND 未找到
 */
event_status_t event_unsubscribe(event_type_t type, uint32_t subscriber_id) {
    EVENT_SYSTEM_VALIDATE();
    if (!g_event_system.initialized) {
        return EVENT_ERR_INVALID_ARG;
    }

    if ((uint32_t) type > MAX_EVENT_TYPE_ID || subscriber_id == EVENT_SUBSCRIBER_ID_INVALID) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];

    event_system_subscriber_id_lock();
    event_system_entry_lock(entry);

    subscriber_entry_t* sub = find_subscriber(entry, subscriber_id);
    if (sub == NULL) {
        event_system_entry_unlock(entry);
        event_system_subscriber_id_unlock();
        return EVENT_ERR_NOT_FOUND;
    }

    sub->is_active = false;
    sub->callback = NULL;
    sub->user_data = NULL;
    entry->subscriber_count--;

    event_system_entry_unlock(entry);
    event_system_subscriber_id_unlock();
    LOG_DBG("Subscriber %d removed from event type %d", subscriber_id, type);
    return EVENT_OK;
}

/**
 * @brief 从所有事件类型中取消订阅
 *
 * @param subscriber_id 订阅者 ID
 */
void event_unsubscribe_all(uint32_t subscriber_id) {
    EVENT_SYSTEM_VALIDATE_VOID();
    if (!g_event_system.initialized || subscriber_id == EVENT_SUBSCRIBER_ID_INVALID) {
        return;
    }

    event_system_subscriber_id_lock();

    /* 注意：event_type_t 是 uint8_t，需要用 int 避免溢出死循环 */
    for (int type = 0; type < MAX_EVENT_TYPES; type++) {
        event_type_entry_t* entry = &g_event_system.event_types[type];

        if (atomic_get(&entry->registered) == 0) {
            continue;
        }

        event_system_entry_lock(entry);

        subscriber_entry_t* sub = find_subscriber(entry, subscriber_id);
        if (sub != NULL) {
            sub->is_active = false;
            sub->callback = NULL;
            sub->user_data = NULL;
            entry->subscriber_count--;
        }

        event_system_entry_unlock(entry);
    }

    event_system_subscriber_id_unlock();

    LOG_DBG("Subscriber %d removed from all event types", subscriber_id);
}

/* =============================================================================
 * 事件发布
 * ============================================================================= */

/**
 * @brief 发布事件（同步方式）
 *
 * @param event 要发布的事件
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_QUEUE_FULL 队列已满
 */
event_status_t event_publish(event_t* event) {
    return event_publish_common(event, EVENT_PUBLISH_QUEUE_OVERFLOW_POLICY, EVENT_PUBLISH_ENQUEUE_TIMEOUT, true);
}

/**
 * @brief 从中断服务程序 (ISR) 发布事件
 *
 * @param event 要发布的事件
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数，EVENT_ERR_QUEUE_FULL 队列已满
 */
event_status_t event_publish_from_isr(event_t* event) {
    /* DROP_LOWEST 仅线程侧重排；ISR 满队列退化为 DROP_NEWEST（见 Kconfig help） */
    return event_publish_common(event, EVENT_PUBLISH_ISR_QUEUE_OVERFLOW_POLICY, K_NO_WAIT, false);
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
    EVENT_SYSTEM_VALIDATE();
    event_t* event = event_create_with_data(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);

    /* 入队成功时 event_publish 已转移动态数据所有权；此处仅释放 event_t 外壳 */
    event_free(event);
    return status;
}

/* =============================================================================
 * 实时安全 API 实现 (Real-time Safe API Implementation)
 * ============================================================================= */

/**
 * @brief 创建事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @return 指向新事件的指针，失败返回 NULL
 */
event_t* event_create_rt(event_type_t type, event_priority_t priority) {
    EVENT_SYSTEM_CHECK_MAGIC_ALLOC();
    event_t* event = NULL;

#if EVENT_SLAB_ENABLED
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);
    int                ret = k_mem_slab_alloc(slab, (void**) &event, K_NO_WAIT);

    if (ret != 0) {
        event_memory_notify_slab_exhausted(priority, event_slab_name_for_priority(priority));
        LOG_WRN("Event slab exhausted for priority %d", priority);
        return NULL;
    }

    event->flags = EVENT_FLAG_FROM_SLAB;
#else
    LOG_DBG("event_create_rt: slab not enabled, returning NULL");
    return NULL;
#endif

    /* 初始化字段 */
    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = 0;
    event->reserved = 0;
    memset(event->data.inline_data, 0, CONFIG_EVENT_INLINE_DATA_SIZE);

    event_debug_track_alloc(event, sizeof(event_t), priority);

    return event;
}

/**
 * @brief 创建带数据的事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要附加的数据指针
 * @param data_len 数据长度（字节）
 * @return 指向新事件的指针，失败返回 NULL
 */
event_t* event_create_with_data_rt(event_type_t type, event_priority_t priority, const void* data, size_t data_len) {
    EVENT_SYSTEM_CHECK_MAGIC_ALLOC();
    if (data == NULL || data_len == 0) {
        return event_create_rt(type, priority);
    }

    /* SIL-2: 统一数据长度验证，与 event_create_with_data 保持一致 */
    if (data_len > 65535) {
        LOG_ERR("Event data length %zu exceeds maximum 64KB", data_len);
        return NULL;
    }

    event_t* event = event_create_rt(type, priority);
    if (event == NULL) {
        return NULL;
    }

    event->data_len = (uint32_t) data_len;

    /* 小数据：内联存储 */
    if (data_len <= CONFIG_EVENT_INLINE_DATA_SIZE) {
        memcpy(event->data.inline_data, data, data_len);
        event->flags |= EVENT_FLAG_DATA_INLINE;
        return event;
    }

    /* 大数据：从 slab 分配，首选最优大小，满时级联到更大的池（MED-NEW-2/3） */
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
    struct k_mem_slab* data_slab = event_memory_select_data_slab(data_len);
    if (data_slab != NULL && event_attach_slab_data(event, data_slab, data, data_len)) {
        return event;
    }
    if (data_slab != NULL) {
        event_memory_notify_slab_exhausted(priority, "event_slab_data");
    }
    /* 首选 slab 已满或不可用，尝试级联 fallback */
    data_slab = event_memory_select_data_slab_with_fallback(data_len);
    if (data_slab != NULL && event_attach_slab_data(event, data_slab, data, data_len)) {
        return event;
    }
    event_free(event);
    event_memory_notify_slab_exhausted(priority, "event_slab_data");
    LOG_WRN("All data slabs exhausted for size %zu", data_len);
    return NULL;
#else
    event_free(event);
    LOG_WRN("Large data requested but no slab configured");
    return NULL;
#endif
}

/**
 * @brief 发布事件并复制数据（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要复制的数据指针
 * @param data_len 数据长度（字节）
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_publish_copy_rt(event_type_t type, event_priority_t priority, const void* data, size_t data_len) {
    EVENT_SYSTEM_VALIDATE();
    event_t* event = event_create_with_data_rt(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);

    event_free(event);
    return status;
}

/**
 * @brief 从 ISR 创建事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 数据指针
 * @param data_len 数据长度
 * @return 事件指针，失败返回 NULL
 *
 * @note 等同于 event_create_with_data_rt，明确 ISR 上下文使用
 */
event_t* event_create_from_isr(event_type_t type, event_priority_t priority, const void* data, size_t data_len) {
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {
        return NULL;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return NULL;
    }
    return event_create_with_data_rt(type, priority, data, data_len);
}

/* =============================================================================
 * 事件创建与内存管理
 * ============================================================================= */

/**
 * @brief 创建新事件
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @return 指向新事件的指针，失败返回 NULL
 */
event_t* event_create(event_type_t type, event_priority_t priority) {
    EVENT_SYSTEM_CHECK_MAGIC_ALLOC();
    /* 优先尝试实时安全路径 */
    event_t* event = event_create_rt(type, priority);
    if (event != NULL) {
        return event;
    }

    /* 回退 k_malloc */
    event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        LOG_ERR("k_malloc failed for event_t");
        return NULL;
    }

    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = 0;
    event->flags = 0; /* 非 FROM_SLAB */
    event->reserved = 0;
    memset(event->data.inline_data, 0, CONFIG_EVENT_INLINE_DATA_SIZE);

    event_debug_track_alloc(event, sizeof(event_t), priority);

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
    EVENT_SYSTEM_CHECK_MAGIC_ALLOC();
    if (data == NULL || data_len == 0) {
        return event_create(type, priority);
    }

    /* 验证数据长度 */
    if (data_len > 65535) {
        LOG_ERR("Event data length %zu exceeds maximum 64KB", data_len);
        return NULL;
    }

    /* 小数据：尝试内联 */
    if (data_len <= CONFIG_EVENT_INLINE_DATA_SIZE) {
        event_t* event = event_create(type, priority);
        if (event == NULL) {
            return NULL;
        }
        event->data_len = (uint32_t) data_len;
        memcpy(event->data.inline_data, data, data_len);
        event->flags |= EVENT_FLAG_DATA_INLINE;
        return event;
    }

    /* 大数据：尝试 slab，首选最优大小，满时级联到更大的池（MED-NEW-3） */
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
    struct k_mem_slab* data_slab = event_memory_select_data_slab(data_len);
    if (data_slab != NULL) {
        event_t* event = event_create(type, priority);
        if (event != NULL) {
            if (event_attach_slab_data(event, data_slab, data, data_len)) {
                return event;
            }
            event_memory_notify_slab_exhausted(priority, "event_slab_data");
            /* 首选 slab 已满，尝试级联 fallback */
            struct k_mem_slab* fallback_slab = event_memory_select_data_slab_with_fallback(data_len);
            if (fallback_slab != NULL && event_attach_slab_data(event, fallback_slab, data, data_len)) {
                return event;
            }
            /* slab 全满，回退到 k_malloc */
            event_free(event);
        }
    }
#endif

    /* 回退 k_malloc */
    event_t* event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        return NULL;
    }
    event->data.ptr = k_malloc(data_len);
    if (event->data.ptr == NULL) {
        k_free(event);
        return NULL;
    }

    /* LOW-NEW-9: 记录真实回退到 k_malloc 的次数 */
    event_memory_inc_fallback_count();

    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = (uint32_t) data_len;
    event->flags = EVENT_FLAG_DATA_DYNAMIC; /* 非 FROM_SLAB */
    event->reserved = 0;
    memcpy(event->data.ptr, data, data_len);

    event_debug_track_alloc(event, sizeof(event_t), priority);
    event_debug_track_alloc(event->data.ptr, data_len, priority);

    return event;
}

/**
 * @brief 释放事件的动态数据
 *
 * SIL-2: 统一的数据释放接口，正确处理来自 slab 池和 k_malloc 的数据。
 * 仅释放 data.ptr，不释放 event_t 本身。
 *
 * @param event 要释放数据的事件
 */
void event_free_data(event_t* event) {
    if (event == NULL) {
        return;
    }

    if ((event->flags & EVENT_FLAG_DATA_DYNAMIC) && event->data.ptr != NULL) {
        event_debug_untrack_alloc(event->data.ptr);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
        if (event->flags & EVENT_FLAG_DATA_FROM_SLAB) {
            struct k_mem_slab* slab =
                event_memory_data_slab_from_flag(event->flags & EVENT_FLAG_SLAB_MASK);

            if (slab == NULL) {
                LOG_ERR("Unknown slab marker for ptr %p (flags=0x%02x)", event->data.ptr, event->flags);
                slab = event_memory_resolve_data_slab_for_ptr(event->data.ptr);
                if (slab == NULL) {
                    LOG_ERR("Cannot resolve slab pool for ptr %p; memory may leak", event->data.ptr);
                }
            }
            if (slab != NULL) {
                k_mem_slab_free(slab, event->data.ptr);
            }
        } else
#endif
        {
            k_free(event->data.ptr);
        }
        event->data.ptr = NULL;
        event->flags &= ~(EVENT_FLAG_DATA_DYNAMIC | EVENT_FLAG_DATA_FROM_SLAB | EVENT_FLAG_SLAB_MASK);
    }
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

    /* SIL-2: 使用统一接口释放动态数据 */
    event_free_data(event);

    event_debug_untrack_alloc(event);

    /* 释放 event_t */
    if (event->flags & EVENT_FLAG_FROM_SLAB) {
#if EVENT_SLAB_ENABLED
        struct k_mem_slab* slab = event_memory_select_event_slab(event->priority);
        k_mem_slab_free(slab, (void*) event);
#else
        /* SIL-2: NEW-3 防御性回退 —— FROM_SLAB 标志置位但 slab 在本编译单元中已禁用。
         * 正常构建中 event_create 不会设置该标志，触发此分支说明：
         *   1) 调用方传入的 event 来自启用 slab 的旁路路径（如跨固件迁移）；
         *   2) 内存被异常修改导致 flags 损坏。
         * 任一情况下都应记录错误供诊断；尝试 k_free 回退避免泄漏，
         * 但若 event 实际来自 slab 池，k_free 行为未定义。 */
        LOG_ERR("Event %p has FROM_SLAB flag but slab is disabled; falling back to k_free", event);
        k_free(event);
#endif
    } else {
        k_free(event);
    }
}

/* =============================================================================
 * 工具函数
 * ============================================================================= */

/**
 * @brief 获取事件类型名称
 *
 * @param type 事件类型 ID
 * @return 事件类型名称字符串
 */
const char* event_get_type_name(event_type_t type) {
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {
        return "UNKNOWN";
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return "CORRUPTED";
    }
    if (!g_event_system.initialized || (uint32_t) type > MAX_EVENT_TYPE_ID) {
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
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return 0;
    }
    if (!g_event_system.initialized || (uint32_t) type > MAX_EVENT_TYPE_ID) {
        return 0;
    }

    event_type_entry_t* entry = &g_event_system.event_types[type];
    event_system_entry_lock(entry);
    uint32_t count = entry->subscriber_count;
    event_system_entry_unlock(entry);

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
    /* LOW-9: 始终先初始化为 0，使调用方在任何返回路径下都能读到确定值。 */
    if (total_events != NULL) {
        *total_events = 0U;
    }
    if (queue_depth != NULL) {
        *queue_depth = 0U;
    }
    if (dropped_events != NULL) {
        *dropped_events = 0U;
    }

    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return;
    }
    if (!g_event_system.initialized) {
        return;
    }

    event_system_stats_lock();

    /* HIGH-NEW-3: 在锁内重检 initialized 状态，防止 shutdown 在窗口期内
     * 释放队列后仍访问 g_event_system.event_queue。 */
    if (!g_event_system.initialized) {
        event_system_stats_unlock();
        return;
    }

    if (total_events != NULL) {
        *total_events = g_event_system.total_events;
    }
    if (queue_depth != NULL && g_event_system.event_queue != NULL) {
        *queue_depth = k_msgq_num_used_get(g_event_system.event_queue);
    }
    if (dropped_events != NULL) {
        *dropped_events = (uint32_t) atomic_get(&g_event_dropped_count);
    }

    event_system_stats_unlock();
}

/**
 * @brief 重置事件系统统计信息
 *
 * SIL-2: 标准版实现，清零所有累积统计值，防止溢出。
 * 由 event_system_compat.c 的 event_compat_reset_statistics() 调用。
 */
void event_system_reset_statistics(void) {
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {
        return;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return;
    }
    if (!g_event_system.initialized) {
        return;
    }

    event_system_stats_lock();
    g_event_system.total_events = 0;
    event_system_stats_unlock();

    atomic_set(&g_event_dropped_count, 0);

    if (g_event_system.event_queue != NULL) {
        event_queue_reset_stats(g_event_system.event_queue);
    }
    event_dispatcher_reset_stats();

    LOG_DBG("Event system statistics reset");
}

/* =============================================================================
 * 内部函数
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
    if (g_event_system.magic == EVENT_SYSTEM_MAGIC_IDLE) {
        return EVENT_ERR_INVALID_ARG;
    }
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        LOG_ERR("Event system magic corruption detected");
        return EVENT_ERR_INVALID_ARG;
    }
    if (event == NULL || (uint32_t) event->type > MAX_EVENT_TYPE_ID) {
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

    event_system_entry_lock(entry);

    if (entry->subscriber_count == 0) {
        event_system_entry_unlock(entry);
        event_system_stats_lock();
        g_event_system.total_events++;
        event_system_stats_unlock();
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

    event_system_entry_unlock(entry);

    /* SIL-2: 在锁外调用所有回调，添加空指针检查 */
    for (uint32_t i = 0; i < n; i++) {
        if (snap[i].cb != NULL) {
            snap[i].cb(event, snap[i].ud);
        } else {
            /* SIL-2: 防御性编程，不应该发生 */
            LOG_ERR("NULL callback in subscriber snapshot at index %u", i);
        }
    }

    event_system_stats_lock();
    g_event_system.total_events++;
    event_system_stats_unlock();

    return EVENT_OK;
}

/**
 * @brief 获取全局事件队列指针
 *
 * @return 指向全局事件队列的指针，未初始化时返回 NULL
 */
struct k_msgq* event_system_get_queue(void) {
    if (g_event_system.magic != EVENT_SYSTEM_MAGIC) {
        return NULL;
    }
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

/* =============================================================================
 * 自动初始化说明 (Auto-initialization Note)
 * ============================================================================= */

/*
 * SIL-2: 标准版事件系统的初始化由 event_system_compat.c 中的
 * event_compat_auto_init() 统一处理。该函数调用 event_compat_init()
 * -> event_system_init() 完成初始化，优先级为 APP_INIT_PRIO_EVENT_SYS。
 *
 * 为避免双重 SYS_INIT 竞态（CRIT-2 修复），event_system.c 中不再注册
 * 独立的 SYS_INIT。event_system_init() 的幂等性保证多次调用安全。
 */
