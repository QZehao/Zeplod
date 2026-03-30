/**
 * @file event_queue.h
 * @brief 事件队列管理头文件 (Event Queue Management Header)
 *
 * 提供基于优先级的队列管理和溢出处理功能。
 *
 * 主要特性：
 * - 基于 Zephyr k_msgq 的高效队列实现
 * - 多种溢出处理策略
 * - 详细的队列统计信息
 * - 水位线监控
 *
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
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
 * 配置宏 (Configuration Macros)
 * ============================================================================= */

/**
 * @brief 队列高水位线阈值
 * @note 当队列深度超过此值时，可触发告警或降级处理
 */
#ifndef CONFIG_EVENT_QUEUE_HIGH_WATERMARK
#define CONFIG_EVENT_QUEUE_HIGH_WATERMARK  (CONFIG_EVENT_QUEUE_SIZE * 3 / 4)
#endif

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 队列溢出处理策略枚举
 */
typedef enum {
    QUEUE_OVERFLOW_DROP_LOWEST,   /**< 丢弃最低优先级的事件 */
    QUEUE_OVERFLOW_DROP_NEWEST,   /**< 丢弃新到达的事件 */
    QUEUE_OVERFLOW_BLOCK          /**< 阻塞等待（不推荐用于实时系统） */
} queue_overflow_policy_t;

/**
 * @brief 队列统计信息结构
 */
typedef struct {
    uint32_t enqueue_count;     /**< 入队操作次数 */
    uint32_t dequeue_count;     /**< 出队操作次数 */
    uint32_t overflow_count;    /**< 溢出发生次数 */
    uint32_t drop_count;        /**< 事件丢弃次数 */
    uint32_t high_watermark;    /**< 队列深度历史最大值 */
} queue_stats_t;

/* =============================================================================
 * 队列 API (Queue API)
 * ============================================================================= */

/**
 * @brief 初始化事件队列
 * 
 * @param queue 指向队列结构的指针
 * @param buffer 队列存储缓冲区
 * @param capacity 队列最大容量
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_queue_init(struct k_msgq *queue, void *buffer, size_t capacity);

/**
 * @brief 入队操作
 * 
 * @param queue 队列实例
 * @param event 要入队的事件
 * @param policy 溢出处理策略
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 *
 * @note 调用前需先对该 queue 执行 event_queue_init()，否则返回 EVENT_ERR_INVALID_ARG
 * @note QUEUE_OVERFLOW_DROP_LOWEST：在队列满时排空并丢弃 priority 数值最大的一条（同值则丢弃 FIFO 最旧）；
 *       若新事件比队列中最差事件还低，则丢弃新事件。需线程上下文，不可在 ISR 中使用。
 *       实现会短暂排空队列再回灌，请避免对同一 k_msgq 并发使用裸 k_msgq_* 与 DROP_LOWEST 混用。
 */
event_status_t event_queue_enqueue(struct k_msgq *queue,
                                    const event_t *event,
                                    queue_overflow_policy_t policy,
                                    k_timeout_t timeout);

/**
 * @brief 出队操作
 * 
 * @param queue 队列实例
 * @param event 输出：出队的事件
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_queue_dequeue(struct k_msgq *queue,
                                    event_t *event,
                                    k_timeout_t timeout);

/**
 * @brief 检查队列是否为空
 * 
 * @param queue 队列实例
 * @return true 队列为空，false 队列非空
 */
bool event_queue_is_empty(const struct k_msgq *queue);

/**
 * @brief 检查队列是否已满
 * 
 * @param queue 队列实例
 * @return true 队列已满，false 队列未满
 */
bool event_queue_is_full(const struct k_msgq *queue);

/**
 * @brief 获取队列深度（已用槽位数）
 * 
 * @param queue 队列实例
 * @return 队列中的事件数量
 */
uint32_t event_queue_depth(const struct k_msgq *queue);

/**
 * @brief 获取队列容量
 * 
 * @param queue 队列实例
 * @return 队列最大容量
 */
uint32_t event_queue_capacity(const struct k_msgq *queue);

/**
 * @brief 清空队列中的所有事件
 * 
 * @param queue 队列实例
 * @note 若队列中事件包含动态负载（is_dynamic=true），会在清空时自动释放 data
 */
void event_queue_purge(struct k_msgq *queue);

/**
 * @brief 获取队列统计信息
 * 
 * @param queue 队列实例
 * @param stats 输出：统计信息结构
 */
void event_queue_get_stats(const struct k_msgq *queue, queue_stats_t *stats);

/**
 * @brief 重置队列统计信息
 * 
 * @param queue 队列实例
 */
void event_queue_reset_stats(struct k_msgq *queue);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_QUEUE_H */
