/**
 * @file event_dispatcher.h
 * @brief 事件分发器头文件 (Event Dispatcher Header)
 *
 * 高性能事件分发器，支持优先级调度和统计功能。
 *
 * 主要特性：
 * - 可配置的分发器线程参数
 * - 事件过滤功能
 * - 详细的统计信息（处理数量、延迟等）
 * - 支持暂停/恢复功能
 * - 支持手动批处理模式
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
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 分发器状态枚举
 */
typedef enum {
    DISPATCHER_STOPPED = 0,  /**< 分发器已停止 */
    DISPATCHER_RUNNING,      /**< 分发器正在运行 */
    DISPATCHER_PAUSED,       /**< 分发器已暂停 */
    DISPATCHER_ERROR         /**< 分发器错误状态 */
} dispatcher_state_t;

/**
 * @brief 分发器配置结构
 * 
 * 用于初始化分发器时的配置参数。
 */
typedef struct {
    uint32_t stack_size;             /**< 分发器线程栈大小（字节） */
    int priority;                    /**< 分发器线程优先级 */
    const char *thread_name;         /**< 分发器线程名称 */
    bool enable_stats;               /**< 是否启用统计信息 */
    uint32_t max_events_per_cycle;   /**< 每个周期最大处理事件数 */
} dispatcher_config_t;

/**
 * @brief 分发器统计信息结构
 * 
 * 包含分发器的运行时统计数据。
 */
typedef struct {
    uint64_t events_processed;  /**< 已处理的事件总数 */
    uint64_t events_dropped;    /**< 被丢弃的事件数 */
    uint32_t max_latency_us;    /**< 最大处理延迟（微秒） */
    uint32_t avg_latency_us;    /**< 平均处理延迟（微秒） */
    uint32_t processing_errors; /**< 处理错误次数 */
} dispatcher_stats_t;

/**
 * @brief 事件过滤函数类型
 * 
 * 用于过滤事件，决定哪些事件需要被处理。
 * 
 * @param event 指向事件的指针
 * @param user_data 用户数据
 * @return true 处理此事件，false 跳过此事件
 */
typedef bool (*event_filter_t)(const event_t *event, void *user_data);

/* =============================================================================
 * 分发器控制 API (Dispatcher Control API)
 * ============================================================================= */

/**
 * @brief 初始化事件分发器
 * 
 * @param config 分发器配置结构指针，NULL 使用默认配置
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 必须先调用 event_system_init() 初始化事件系统
 * @note 默认配置：
 *   - stack_size: CONFIG_EVENT_DISPATCHER_STACK_SIZE
 *   - priority: CONFIG_EVENT_DISPATCHER_PRIORITY
 *   - thread_name: "event_disp"
 *   - enable_stats: true
 *   - max_events_per_cycle: 100
 */
event_status_t event_dispatcher_init(const dispatcher_config_t *config);

/**
 * @brief 启动分发器
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_dispatcher_start(void);

/**
 * @brief 停止分发器
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_dispatcher_stop(void);

/**
 * @brief 暂停事件处理
 * 
 * 暂停后，分发器线程仍存在但不处理事件。
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_dispatcher_pause(void);

/**
 * @brief 恢复事件处理
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 */
event_status_t event_dispatcher_resume(void);

/**
 * @brief 获取分发器当前状态
 * 
 * @return 当前分发器状态
 */
dispatcher_state_t event_dispatcher_get_state(void);

/* =============================================================================
 * 事件处理 API (Event Processing API)
 * ============================================================================= */

/**
 * @brief 设置事件过滤器
 * 
 * 过滤器用于决定哪些事件需要被处理。
 * 
 * @param filter 过滤函数指针
 * @param user_data 用户数据，将传入过滤函数
 */
void event_dispatcher_set_filter(event_filter_t filter, void *user_data);

/**
 * @brief 清除事件过滤器
 * 
 * 清除后，所有事件都将被处理。
 */
void event_dispatcher_clear_filter(void);

/**
 * @brief 处理单个事件（用于手动分发模式）
 * 
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功处理了事件，其他错误码见 event_status_t
 * 
 * @note 此函数用于手动控制事件处理流程
 * @note 返回 EVENT_ERR_QUEUE_EMPTY 表示队列中无事件
 */
event_status_t event_dispatcher_process_one(k_timeout_t timeout);

/**
 * @brief 处理所有待处理事件
 * 
 * @param max_events 最大处理事件数，0 表示无限制
 * @return 已处理的事件数量
 * 
 * @note 此函数会一直处理直到队列为空或达到 max_events 限制
 */
uint32_t event_dispatcher_process_all(uint32_t max_events);

/* =============================================================================
 * 统计 API (Statistics API)
 * ============================================================================= */

/**
 * @brief 获取分发器统计信息
 * 
 * @param stats 输出：统计信息结构指针
 */
void event_dispatcher_get_stats(dispatcher_stats_t *stats);

/**
 * @brief 重置分发器统计信息
 * 
 * 将所有统计计数器清零。
 */
void event_dispatcher_reset_stats(void);

/**
 * @brief 获取当前事件延迟
 * 
 * @return 延迟时间（微秒）
 */
uint32_t event_dispatcher_get_current_latency(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_DISPATCHER_H */
