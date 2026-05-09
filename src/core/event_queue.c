/**
 * @file event_queue.c
 * @brief 事件队列实现 (Event Queue Implementation)
 *
 * 基于优先级的队列实现，支持可配置的溢出处理。
 *
 * 实现说明：
 * - 基于 Zephyr k_msgq 实现
 * - 支持多种溢出策略
 * - 提供详细的统计信息
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

#include "event_queue.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(event_queue, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构 (Internal Data Structures)
 * ============================================================================= */

/**
 * @brief 扩展队列控制块
 *
 * 包含队列的统计信息和管理数据。
 */
typedef struct {
    struct k_msgq* msgq;                /**< 消息队列指针 */
    queue_stats_t  stats;               /**< 队列统计信息 */
    uint32_t       capacity;            /**< 队列容量 */
    struct k_mutex stats_lock;          /**< 保护统计信息的互斥锁 */
    struct k_mutex reorder_lock;        /**< DROP_LOWEST 排空/回灌时与并发入队互斥 */
    event_t*       drop_lowest_scratch; /**< DROP_LOWEST 独立临时缓冲区 */
} event_queue_cb_t;

/* 静态队列控制块数组，用于跟踪统计信息 */
/* SIL-2: 增加数组大小以支持更多测试场景和并发队列 */
#define MAX_QUEUE_CB_ENTRIES 32

static event_queue_cb_t g_queue_cb[MAX_QUEUE_CB_ENTRIES];

/**
 * @brief 验证事件有效性
 */
static bool event_is_valid(const event_t* event) {
    if (event == NULL) {
        return false;
    }
    /* SIL-2: 验证事件类型范围 */
    if (event->type >= 256) {
        return false;
    }
    return true;
}

static void event_free_queued_payload(event_t* ev) {
    /* SIL-2: 使用统一接口释放动态数据，正确处理 slab 来源 */
    event_free_data(ev);
}

/**
 * 队列已满时：丢弃队列中优先级最低的一条（priority 数值最大；相等则 FIFO 最旧），再入队 event。
 * 若 event 比队列中最差的一条还差，则丢弃 event（不入队）。
 */
static event_status_t enqueue_drop_lowest(struct k_msgq* queue, const event_t* event, k_timeout_t timeout,
                                          event_queue_cb_t* cb) {
    if (k_is_in_isr()) {
        LOG_WRN("QUEUE_OVERFLOW_DROP_LOWEST is not supported from ISR");
        return EVENT_ERR_INVALID_ARG;
    }

    /* SIL-2: 验证输入事件有效性 */
    if (!event_is_valid(event)) {
        LOG_ERR("Invalid event in enqueue_drop_lowest");
        return EVENT_ERR_INVALID_ARG;
    }

    struct k_msgq_attrs attrs;

    k_msgq_get_attrs(queue, &attrs);

    /* SIL-2: 检查 scratch 缓冲区是否已分配 */
    if (cb->drop_lowest_scratch == NULL) {
        LOG_ERR("DROP_LOWEST scratch not allocated");
        return EVENT_ERR_INVALID_ARG;
    }

    if (attrs.max_msgs > cb->capacity) {
        LOG_ERR("Queue capacity exceeds DROP_LOWEST scratch capacity");
        return EVENT_ERR_INVALID_ARG;
    }

    k_mutex_lock(&cb->reorder_lock, K_FOREVER);

    k_msgq_get_attrs(queue, &attrs);
    uint32_t n = attrs.used_msgs;

    if (n < attrs.max_msgs) {
        int pret = k_msgq_put(queue, event, timeout);

        k_mutex_unlock(&cb->reorder_lock);

        if (pret != 0) {
            if (pret == -ENOMSG) {
                return EVENT_ERR_QUEUE_FULL;
            }
            return EVENT_ERR_TIMEOUT;
        }

        k_mutex_lock(&cb->stats_lock, K_FOREVER);
        cb->stats.enqueue_count++;
        {
            uint32_t depth = k_msgq_num_used_get(queue);

            if (depth > cb->stats.high_watermark) {
                cb->stats.high_watermark = depth;
            }
        }
        k_mutex_unlock(&cb->stats_lock);

        return EVENT_OK;
    }

    for (uint32_t i = 0; i < n; i++) {
        if (k_msgq_get(queue, &cb->drop_lowest_scratch[i], K_NO_WAIT) != 0) {
            LOG_ERR("DROP_LOWEST drain failed at %u", i);
            k_mutex_unlock(&cb->reorder_lock);
            return EVENT_ERR_QUEUE_FULL;
        }
    }

    uint32_t worst = 0U;

    for (uint32_t i = 1U; i < n; i++) {
        if (cb->drop_lowest_scratch[i].priority > cb->drop_lowest_scratch[worst].priority) {
            worst = i;
        }
    }

    if (event->priority > cb->drop_lowest_scratch[worst].priority) {
        for (uint32_t i = 0; i < n; i++) {
            (void) k_msgq_put(queue, &cb->drop_lowest_scratch[i], K_NO_WAIT);
        }
        k_mutex_unlock(&cb->reorder_lock);
        k_mutex_lock(&cb->stats_lock, K_FOREVER);
        cb->stats.drop_count++;
        k_mutex_unlock(&cb->stats_lock);
        LOG_DBG("Queue full, incoming lower than worst queued; drop newest");
        return EVENT_ERR_QUEUE_FULL;
    }

    event_free_queued_payload(&cb->drop_lowest_scratch[worst]);

    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.drop_count++;
    k_mutex_unlock(&cb->stats_lock);

    for (uint32_t i = 0; i < n; i++) {
        if (i != worst) {
            (void) k_msgq_put(queue, &cb->drop_lowest_scratch[i], K_NO_WAIT);
        }
    }

    int ret = k_msgq_put(queue, event, timeout);

    k_mutex_unlock(&cb->reorder_lock);

    if (ret != 0) {
        if (ret == -ENOMSG) {
            return EVENT_ERR_QUEUE_FULL;
        }
        return EVENT_ERR_TIMEOUT;
    }

    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.enqueue_count++;
    {
        uint32_t depth = k_msgq_num_used_get(queue);

        if (depth > cb->stats.high_watermark) {
            cb->stats.high_watermark = depth;
        }
    }
    k_mutex_unlock(&cb->stats_lock);

    return EVENT_OK;
}

/**
 * @brief 获取消息队列属性（常量版本）
 *
 * @param queue 队列指针（只读）
 * @param attrs 输出：属性结构
 */
static void msgq_get_attrs_const(const struct k_msgq* queue, struct k_msgq_attrs* attrs) {
    k_msgq_get_attrs((struct k_msgq*) queue, attrs);
}

static event_queue_cb_t* event_queue_find_cb(const struct k_msgq* queue) {
    for (size_t i = 0; i < MAX_QUEUE_CB_ENTRIES; i++) {
        if (g_queue_cb[i].msgq == queue) {
            return &g_queue_cb[i];
        }
    }
    return NULL;
}

static event_queue_cb_t* event_queue_alloc_cb(struct k_msgq* queue) {
    event_queue_cb_t* free_cb = NULL;

    for (size_t i = 0; i < MAX_QUEUE_CB_ENTRIES; i++) {
        if (g_queue_cb[i].msgq == queue) {
            return &g_queue_cb[i];
        }
        if (g_queue_cb[i].msgq == NULL && free_cb == NULL) {
            free_cb = &g_queue_cb[i];
        }
    }

    return free_cb;
}

/* =============================================================================
 * 队列 API 实现 (Queue API Implementation)
 * ============================================================================= */

/**
 * @brief 初始化事件队列
 *
 * @param queue 队列指针
 * @param buffer 缓冲区指针
 * @param capacity 队列容量
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 无效参数
 */
event_status_t event_queue_init(struct k_msgq* queue, void* buffer, size_t capacity) {
    if (queue == NULL || buffer == NULL || capacity == 0) {
        return EVENT_ERR_INVALID_ARG;
    }

    k_msgq_init(queue, buffer, sizeof(event_t), capacity);

    /* 初始化统计信息 */
    event_queue_cb_t* cb = event_queue_alloc_cb(queue);
    if (cb == NULL) {
        LOG_ERR("No available queue control block");
        return EVENT_ERR_NO_MEM;
    }

    cb->msgq = queue;
    cb->capacity = capacity;
    cb->stats = (queue_stats_t) {0};
    k_mutex_init(&cb->stats_lock);
    k_mutex_init(&cb->reorder_lock);

    /* SIL-2: 为每个队列独立分配 DROP_LOWEST scratch 缓冲区，消除全局并发瓶颈 */
    cb->drop_lowest_scratch = (event_t*) k_malloc(capacity * sizeof(event_t));
    if (cb->drop_lowest_scratch == NULL) {
        cb->msgq = NULL; /* 回滚标记为未使用 */
        LOG_ERR("Failed to allocate drop_lowest_scratch for queue");
        return EVENT_ERR_NO_MEM;
    }

    LOG_DBG("Event queue initialized: capacity=%d", capacity);
    return EVENT_OK;
}

/**
 * @brief 入队操作
 *
 * @param queue 队列指针
 * @param event 要入队的事件
 * @param policy 溢出处理策略
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_queue_enqueue(struct k_msgq* queue, const event_t* event, queue_overflow_policy_t policy,
                                   k_timeout_t timeout) {
    if (queue == NULL || event == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        return EVENT_ERR_INVALID_ARG;
    }

    struct k_msgq_attrs qattrs;

    msgq_get_attrs_const(queue, &qattrs);

    /* SIL-2: 验证事件类型有效性 */
    if (event->type >= 256) {
        LOG_ERR("Invalid event type in enqueue: %d", event->type);
        return EVENT_ERR_INVALID_ARG;
    }

    /* 检查队列是否已满（用 msgq 属性，避免未调用 event_queue_init 时 capacity 为 0） */
    if (qattrs.used_msgs >= qattrs.max_msgs) {
        k_mutex_lock(&cb->stats_lock, K_FOREVER);
        cb->stats.overflow_count++;
        k_mutex_unlock(&cb->stats_lock);

        switch (policy) {
        case QUEUE_OVERFLOW_DROP_NEWEST:
            LOG_DBG("Queue full, dropping newest event");
            return EVENT_ERR_QUEUE_FULL;

        case QUEUE_OVERFLOW_DROP_LOWEST:
            return enqueue_drop_lowest(queue, event, timeout, cb);

        case QUEUE_OVERFLOW_BLOCK:
            /* SIL-2: 阻塞等待，添加日志 */
            LOG_DBG("Queue full, blocking until space available");
            break;

        default:
            /* SIL-2: 防御性编程，处理未知策略 */
            LOG_ERR("Unknown overflow policy: %d", policy);
            return EVENT_ERR_INVALID_ARG;
        }
    }

    int ret = k_msgq_put(queue, event, timeout);
    if (ret != 0) {
        if (ret == -ENOMSG) {
            return EVENT_ERR_QUEUE_FULL;
        }
        return EVENT_ERR_TIMEOUT;
    }

    /* 更新统计信息 */
    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.enqueue_count++;

    uint32_t current_depth = k_msgq_num_used_get(queue);
    if (current_depth > cb->stats.high_watermark) {
        cb->stats.high_watermark = current_depth;
    }
    k_mutex_unlock(&cb->stats_lock);

    return EVENT_OK;
}

/**
 * @brief 出队操作
 *
 * @param queue 队列指针
 * @param event 输出：出队的事件
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_queue_dequeue(struct k_msgq* queue, event_t* event, k_timeout_t timeout) {
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

    /* SIL-2: find_cb 失败时，事件已从队列移除，需要释放动态负载防止泄漏 */
    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        event_free_queued_payload(event);
        return EVENT_ERR_INVALID_ARG;
    }

    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats.dequeue_count++;
    k_mutex_unlock(&cb->stats_lock);

    return EVENT_OK;
}

/**
 * @brief 检查队列是否为空
 *
 * @param queue 队列指针
 * @return true 队列为空，false 队列非空
 */
bool event_queue_is_empty(const struct k_msgq* queue) {
    if (queue == NULL) {
        return true;
    }

    struct k_msgq_attrs attrs;

    msgq_get_attrs_const(queue, &attrs);
    return attrs.used_msgs == 0U;
}

/**
 * @brief 检查队列是否已满
 *
 * @param queue 队列指针
 * @return true 队列已满，false 队列未满
 */
bool event_queue_is_full(const struct k_msgq* queue) {
    if (queue == NULL) {
        return false;
    }

    struct k_msgq_attrs attrs;

    msgq_get_attrs_const(queue, &attrs);
    return attrs.used_msgs >= attrs.max_msgs;
}

/**
 * @brief 获取队列深度
 *
 * @param queue 队列指针
 * @return 队列中的事件数量
 */
uint32_t event_queue_depth(const struct k_msgq* queue) {
    if (queue == NULL) {
        return 0U;
    }

    struct k_msgq_attrs attrs;

    msgq_get_attrs_const(queue, &attrs);
    return attrs.used_msgs;
}

/**
 * @brief 获取队列容量
 *
 * @param queue 队列指针
 * @return 队列最大容量
 */
uint32_t event_queue_capacity(const struct k_msgq* queue) {
    if (queue == NULL) {
        return 0U;
    }

    struct k_msgq_attrs attrs;

    msgq_get_attrs_const(queue, &attrs);
    return attrs.max_msgs;
}

/**
 * @brief 清空队列
 *
 * @param queue 队列指针
 */
void event_queue_purge(struct k_msgq* queue) {
    if (queue == NULL) {
        return;
    }

    event_queue_cb_t* cb = event_queue_find_cb(queue);
    event_t           ev;
    uint32_t          purged = 0U;

    while (k_msgq_get(queue, &ev, K_NO_WAIT) == 0) {
        event_free_queued_payload(&ev);
        purged++;
    }
    /* SIL-2: 不移除 k_msgq_purge；此时 k_msgq_get 循环已清空队列。
     * 若此处再调用 k_msgq_purge，两次操作之间新入队的事件的动态负载将无法释放。 */

    if (cb != NULL && purged > 0U) {
        k_mutex_lock(&cb->stats_lock, K_FOREVER);
        cb->stats.drop_count += purged;
        k_mutex_unlock(&cb->stats_lock);
    }

    LOG_DBG("Event queue purged, dropped=%u", purged);
}

/**
 * @brief 获取队列统计信息
 *
 * @param queue 队列指针
 * @param stats 输出：统计信息结构
 */
void event_queue_get_stats(const struct k_msgq* queue, queue_stats_t* stats) {
    if (queue == NULL || stats == NULL) {
        return;
    }

    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        *stats = (queue_stats_t) {0};
        return;
    }

    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    *stats = cb->stats;
    k_mutex_unlock(&cb->stats_lock);
}

/**
 * @brief 重置队列统计信息
 *
 * @param queue 队列指针
 */
void event_queue_reset_stats(struct k_msgq* queue) {
    if (queue == NULL) {
        return;
    }

    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        return;
    }

    k_mutex_lock(&cb->stats_lock, K_FOREVER);
    cb->stats = (queue_stats_t) {0};
    k_mutex_unlock(&cb->stats_lock);

    LOG_DBG("Queue statistics reset");
}
