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
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(event_queue, CONFIG_SYS_LOG_LEVEL);

/* 外部声明：来自 event_system.c 的全局丢弃计数器递增函数 */
extern void event_system_inc_dropped_count(void);

/* =============================================================================
 * 内部数据结构 (Internal Data Structures)
 * ============================================================================= */

/**
 * @brief 扩展队列控制块
 *
 * 包含队列的统计信息和管理数据。
 *
 * SIL-2: HIGH-3 修复 —— 统计计数器改为 atomic_t，使 ISR 路径可正确累计
 * （k_msgq_put 在 ISR 中合法，但互斥锁不可用，原子操作填补此空白）。
 */
typedef struct {
    struct k_msgq* msgq;                /**< 消息队列指针 */
    atomic_t       enqueue_count;       /**< 入队成功计数（ISR 安全） */
    atomic_t       dequeue_count;       /**< 出队成功计数 */
    atomic_t       overflow_count;      /**< 溢出（队列满）计数（ISR 安全） */
    atomic_t       drop_count;          /**< 显式丢弃计数（DROP_LOWEST/purge） */
    atomic_t       high_watermark;      /**< 队列深度历史最大值（CAS 更新） */
    uint32_t       capacity;            /**< 队列容量 */
    struct k_mutex reorder_lock;        /**< DROP_LOWEST 排空/回灌时与并发入队互斥 */
    event_t*       drop_lowest_scratch; /**< DROP_LOWEST 独立临时缓冲区 */
} event_queue_cb_t;

/* 静态队列控制块数组，用于跟踪统计信息 */
/* SIL-2: 增加数组大小以支持更多测试场景和并发队列 */
#define MAX_QUEUE_CB_ENTRIES 32

static event_queue_cb_t g_queue_cb[MAX_QUEUE_CB_ENTRIES];

/** 保护队列控制块数组的全局互斥锁 */
static K_MUTEX_DEFINE(g_queue_cb_lock);

/**
 * @brief 验证事件有效性
 */
static bool event_is_valid(const event_t* event) {
    if (event == NULL) {
        return false;
    }
    /* event->type 是 uint8_t，值域天然为 0-255，无需额外范围检查 */
    return true;
}

static void event_free_queued_payload(event_t* ev) {
    /* SIL-2: 使用统一接口释放动态数据，正确处理 slab 来源 */
    event_free_data(ev);
}

/**
 * @brief 原子更新水位线（仅当新值大于当前值时更新）
 *
 * SIL-2: HIGH-3 修复后的 CAS 循环实现，避免并发更新丢失。
 * ISR 安全：原子操作不依赖互斥锁。
 *
 * @param hw 水位线 atomic 指针
 * @param depth 当前深度
 */
static inline void update_high_watermark(atomic_t* hw, uint32_t depth) {
    atomic_val_t old_hw;

    do {
        old_hw = atomic_get(hw);
        if ((atomic_val_t) depth <= old_hw) {
            return;
        }
    } while (!atomic_cas(hw, old_hw, (atomic_val_t) depth));
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

        atomic_inc(&cb->enqueue_count);
        update_high_watermark(&cb->high_watermark, k_msgq_num_used_get(queue));

        return EVENT_OK;
    }

    for (uint32_t i = 0; i < n; i++) {
        if (k_msgq_get(queue, &cb->drop_lowest_scratch[i], K_NO_WAIT) != 0) {
            LOG_ERR("DROP_LOWEST drain failed at %u, restoring %u events", i, i);
            /* SIL-2: 回灌已出队的事件，避免事件丢失和内存泄漏 */
            for (uint32_t j = 0; j < i; j++) {
                if (k_msgq_put(queue, &cb->drop_lowest_scratch[j], K_NO_WAIT) != 0) {
                    /* 队列在我们持有 reorder_lock 时变满，理论不应发生 */
                    event_free_queued_payload(&cb->drop_lowest_scratch[j]);
                }
            }
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
        atomic_inc(&cb->drop_count);
        event_system_inc_dropped_count();
        LOG_DBG("Queue full, incoming lower than worst queued; drop newest");
        return EVENT_ERR_QUEUE_FULL;
    }

    event_free_queued_payload(&cb->drop_lowest_scratch[worst]);

    atomic_inc(&cb->drop_count);
    event_system_inc_dropped_count();

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

    atomic_inc(&cb->enqueue_count);
    update_high_watermark(&cb->high_watermark, k_msgq_num_used_get(queue));

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
    k_mutex_lock(&g_queue_cb_lock, K_FOREVER);
    for (size_t i = 0; i < MAX_QUEUE_CB_ENTRIES; i++) {
        if (g_queue_cb[i].msgq == queue) {
            k_mutex_unlock(&g_queue_cb_lock);
            return &g_queue_cb[i];
        }
    }
    k_mutex_unlock(&g_queue_cb_lock);
    return NULL;
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

    /* SIL-2: HIGH-NEW-2 —— 全程持有 g_queue_cb_lock，确保重复检测、槽位分配、
     * k_msgq_init 和 cb 初始化构成原子序列。任何中途释放锁的做法都会引入 TOCTOU
     * 竞态，允许多线程并发执行 k_msgq_init 导致队列等待队列损坏。 */
    k_mutex_lock(&g_queue_cb_lock, K_FOREVER);

    /* 在持锁状态下完成所有检查与初始化 */
    for (size_t i = 0; i < MAX_QUEUE_CB_ENTRIES; i++) {
        if (g_queue_cb[i].msgq == queue) {
            k_mutex_unlock(&g_queue_cb_lock);
            LOG_WRN("Queue already initialized, skipping");
            return EVENT_OK; /* 已初始化 */
        }
    }

    /* 寻找空槽 */
    event_queue_cb_t* cb = NULL;
    for (size_t i = 0; i < MAX_QUEUE_CB_ENTRIES; i++) {
        if (g_queue_cb[i].msgq == NULL) {
            cb = &g_queue_cb[i];
            break;
        }
    }

    if (cb == NULL) {
        k_mutex_unlock(&g_queue_cb_lock);
        LOG_ERR("No available queue control block");
        return EVENT_ERR_NO_MEM;
    }

    k_msgq_init(queue, buffer, sizeof(event_t), capacity);

    cb->msgq = queue;
    cb->capacity = capacity;
    atomic_set(&cb->enqueue_count, 0);
    atomic_set(&cb->dequeue_count, 0);
    atomic_set(&cb->overflow_count, 0);
    atomic_set(&cb->drop_count, 0);
    atomic_set(&cb->high_watermark, 0);
    k_mutex_init(&cb->reorder_lock);

    /* SIL-2: 为每个队列独立分配 DROP_LOWEST scratch 缓冲区，消除全局并发瓶颈 */
    cb->drop_lowest_scratch = (event_t*) k_malloc(capacity * sizeof(event_t));
    if (cb->drop_lowest_scratch == NULL) {
        cb->msgq = NULL; /* 回滚标记为未使用 */
        k_mutex_unlock(&g_queue_cb_lock);
        LOG_ERR("Failed to allocate drop_lowest_scratch for queue");
        return EVENT_ERR_NO_MEM;
    }

    k_mutex_unlock(&g_queue_cb_lock);

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

    /* SIL-2: 先尝试入队，使用返回值判断真实结果，避免 TOCTOU 竞态。
     * 之前的实现先检查 used_msgs >= max_msgs 再调用 k_msgq_put，
     * 在这两个操作之间其他线程可能出队，导致 overflow_count 被错误递增。 */
    int ret = k_msgq_put(queue, event, timeout);
    if (ret == 0) {
        /* SIL-2: HIGH-3 修复后使用原子操作，ISR 路径也能正确累计统计。 */
        atomic_inc(&cb->enqueue_count);
        update_high_watermark(&cb->high_watermark, k_msgq_num_used_get(queue));
        return EVENT_OK;
    }

    /* 入队失败：仅队列满（-ENOMSG）计入 overflow_count，超时（-EAGAIN）不计入。
     * SIL-2: HIGH-3 修复后使用原子操作，ISR 路径安全。 */
    if (ret == -ENOMSG) {
        atomic_inc(&cb->overflow_count);
        event_system_inc_dropped_count();
    }

    /* 区分超时和队列满 */
    if (ret == -EAGAIN) {
        return EVENT_ERR_TIMEOUT;
    }

    switch (policy) {
    case QUEUE_OVERFLOW_DROP_NEWEST:
        LOG_DBG("Queue full, dropping newest event");
        return EVENT_ERR_QUEUE_FULL;

    case QUEUE_OVERFLOW_DROP_LOWEST:
        return enqueue_drop_lowest(queue, event, timeout, cb);

    case QUEUE_OVERFLOW_BLOCK:
        /* SIL-2: BLOCK 策略下 k_msgq_put 已使用 timeout 阻塞等待。
         * 如果 k_msgq_put 返回非 0，说明阻塞期间仍未获得空间（超时或中断）。
         * 此时返回队列满错误。 */
        LOG_DBG("Queue full, blocking timed out");
        return EVENT_ERR_QUEUE_FULL;

    default:
        /* SIL-2: 防御性编程，处理未知策略 */
        LOG_ERR("Unknown overflow policy: %d", policy);
        return EVENT_ERR_INVALID_ARG;
    }
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

    /* SIL-2: find_cb 失败时，事件已从队列移除，需要释放动态负载防止泄漏。
     * 此情况说明队列未通过 event_queue_init() 初始化，属于编程错误。 */
    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        LOG_ERR("Queue not initialized via event_queue_init(); event lost, data freed");
        event_free_queued_payload(event);
        return EVENT_ERR_INVALID_ARG;
    }

    atomic_inc(&cb->dequeue_count);

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

    /* SIL-2: 获取 reorder_lock，与 enqueue_drop_lowest 互斥，防止 drain-refill 窗口竞态 */
    if (cb != NULL) {
        k_mutex_lock(&cb->reorder_lock, K_FOREVER);
    }

    while (k_msgq_get(queue, &ev, K_NO_WAIT) == 0) {
        event_free_queued_payload(&ev);
        purged++;
    }
    /* SIL-2: 不移除 k_msgq_purge；此时 k_msgq_get 循环已清空队列。
     * 若此处再调用 k_msgq_purge，两次操作之间新入队的事件的动态负载将无法释放。 */

    if (cb != NULL) {
        k_mutex_unlock(&cb->reorder_lock);
    }

    if (cb != NULL && purged > 0U) {
        atomic_add(&cb->drop_count, (atomic_val_t) purged);
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

    /* SIL-2: HIGH-3 修复后从 atomic 计数器重建快照。
     * 各计数器独立读取，整体快照非原子（不同字段对应不同瞬时值），
     * 但相比丢失 ISR 路径统计，此妥协可接受。 */
    stats->enqueue_count = (uint32_t) atomic_get(&cb->enqueue_count);
    stats->dequeue_count = (uint32_t) atomic_get(&cb->dequeue_count);
    stats->overflow_count = (uint32_t) atomic_get(&cb->overflow_count);
    stats->drop_count = (uint32_t) atomic_get(&cb->drop_count);
    stats->high_watermark = (uint32_t) atomic_get(&cb->high_watermark);
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

    atomic_set(&cb->enqueue_count, 0);
    atomic_set(&cb->dequeue_count, 0);
    atomic_set(&cb->overflow_count, 0);
    atomic_set(&cb->drop_count, 0);
    atomic_set(&cb->high_watermark, 0);

    LOG_DBG("Queue statistics reset");
}

/**
 * @brief 反初始化事件队列
 *
 * SIL-2: 释放队列初始化时分配的所有动态资源，防止内存泄漏。
 * 包括 DROP_LOWEST scratch 缓冲区和控制块状态清理。
 *
 * @param queue 队列实例
 */
void event_queue_deinit(struct k_msgq* queue) {
    if (queue == NULL) {
        return;
    }

    event_queue_cb_t* cb = event_queue_find_cb(queue);
    if (cb == NULL) {
        return;
    }

    /* SIL-2: 先清空队列中剩余的事件，防止动态负载泄漏 */
    event_queue_purge(queue);

    /* SIL-2: 释放 DROP_LOWEST scratch 缓冲区（CRIT-1 修复） */
    if (cb->drop_lowest_scratch != NULL) {
        k_free(cb->drop_lowest_scratch);
        cb->drop_lowest_scratch = NULL;
    }

    /* 清理控制块状态 */
    cb->msgq = NULL;
    cb->capacity = 0;
    atomic_set(&cb->enqueue_count, 0);
    atomic_set(&cb->dequeue_count, 0);
    atomic_set(&cb->overflow_count, 0);
    atomic_set(&cb->drop_count, 0);
    atomic_set(&cb->high_watermark, 0);

    LOG_DBG("Event queue deinitialized");
}
