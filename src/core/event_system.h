/**
 * @file event_system.h
 * @brief 核心事件系统头文件 (Core Event System Header)
 *
 * 为 Zephyr RTOS 设计的高性能实时事件系统。
 * 提供发布 - 订阅模式，支持线程安全操作。
 *
 * 主要特性：
 * - 支持最多 256 种事件类型
 * - 支持多个订阅者 per 事件类型
 * - 线程安全的发布/订阅操作
 * - 支持 ISR 上下文发布事件
 * - 动态事件数据分配
 *
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
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
 * 配置宏 (Configuration Macros)
 * 可通过 Kconfig 或 prj.conf 覆盖默认值
 * ============================================================================= */

/**
 * @brief 事件队列大小（最多容纳的事件数量）
 * @note 队列满时，新事件将被丢弃
 */
#ifndef CONFIG_EVENT_QUEUE_SIZE
#define CONFIG_EVENT_QUEUE_SIZE 64
#endif

/**
 * @brief 每个事件类型最多支持的订阅者数量
 */
#ifndef CONFIG_EVENT_MAX_SUBSCRIBERS
#define CONFIG_EVENT_MAX_SUBSCRIBERS 16
#endif

/**
 * @brief 事件分发器线程栈大小（字节）
 */
#ifndef CONFIG_EVENT_DISPATCHER_STACK_SIZE
#define CONFIG_EVENT_DISPATCHER_STACK_SIZE 2048
#endif

/**
 * @brief 事件分发器线程优先级
 * @note Zephyr 中数值越小优先级越高，0 为最高优先级
 */
#ifndef CONFIG_EVENT_DISPATCHER_PRIORITY
#define CONFIG_EVENT_DISPATCHER_PRIORITY 5
#endif

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 事件类型标识符（支持最多 256 种事件类型）
 * @note 取值范围：0-255
 */
typedef uint8_t event_type_t;

/** 
 * @brief 预定义的事件类型 ID
 * @note 可在各模块中注册具体名称
 */
#define EVENT_TYPE_GENERIC        1U   /**< 通用事件类型 */
#define EVENT_TYPE_SENSOR_DATA    10U  /**< 传感器数据事件 */
#define EVENT_TYPE_SENSOR_CONFIG  11U  /**< 传感器配置事件 */
/** 
 * @brief Thread IPC Service 结果事件
 * @note 与 CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE 配合使用
 */
#define EVENT_TYPE_THREAD_IPC_RESPONSE 20U

/**
 * @brief 事件优先级枚举
 * @note 数值越小优先级越高，与 Zephyr 线程优先级约定一致
 */
typedef enum {
    EVENT_PRIORITY_LOW      = 10,  /**< 低优先级事件 */
    EVENT_PRIORITY_NORMAL   = 5,   /**< 普通优先级事件 */
    EVENT_PRIORITY_HIGH     = 2,   /**< 高优先级事件 */
    EVENT_PRIORITY_CRITICAL = 0    /**< 临界优先级事件（最高） */
} event_priority_t;

/**
 * @brief 事件状态码枚举
 * @note 所有事件 API 均返回此枚举值
 */
typedef enum {
    EVENT_OK                = 0,   /**< 操作成功 */
    EVENT_ERR_NO_MEM        = -1,  /**< 内存不足 */
    EVENT_ERR_QUEUE_FULL    = -2,  /**< 队列已满 */
    EVENT_ERR_QUEUE_EMPTY   = -3,  /**< 队列为空 */
    EVENT_ERR_INVALID_ARG   = -4,  /**< 无效参数 */
    EVENT_ERR_NOT_FOUND     = -5,  /**< 未找到 */
    EVENT_ERR_NO_SUBSCRIBER = -6,  /**< 无订阅者 */
    EVENT_ERR_TIMEOUT       = -7   /**< 操作超时 */
} event_status_t;

/**
 * @brief 事件数据结构
 * 
 * 所有通过事件系统传递的数据都封装在此结构中。
 * 支持静态和动态数据两种方式：
 * - 静态数据：data 指向外部管理的内存，is_dynamic = false
 * - 动态数据：data 通过 k_malloc 分配，is_dynamic = true，由 event_free() 释放
 * 
 * @note 事件对象本身不直接放入队列，队列中存储的是 event_t 的副本
 * @note 如果 data 是动态分配的，确保在事件处理后正确释放
 */
typedef struct {
    event_type_t    type;           /**< 事件类型标识符 */
    event_priority_t priority;      /**< 事件优先级 */
    uint32_t        timestamp;      /**< 事件创建时间戳（毫秒 uptime） */
    uint32_t        source_id;      /**< 源模块/组件 ID */
    uint32_t        data_len;       /**< 事件数据长度（字节） */
    void           *data;           /**< 事件数据指针 */
    bool            is_dynamic;     /**< true 表示 data 是动态分配的 */
} event_t;

/**
 * @brief 事件回调函数类型
 * 
 * 订阅者通过此回调函数接收事件通知。
 * 
 * @param event 指向事件的指针（只读，不要修改）
 * @param user_data 订阅时传入的用户数据
 * 
 * @note 回调在分发器线程上下文中执行，避免长时间阻塞操作
 * @note 不要在此回调中释放 event 或 event->data
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief 订阅者条目结构
 * 
 * 每个订阅者在注册时都会创建一个此结构。
 */
typedef struct {
    event_callback_t callback;      /**< 回调函数指针 */
    void            *user_data;     /**< 回调用户数据 */
    uint32_t         subscriber_id; /**< 唯一订阅者 ID（由系统分配） */
    bool             is_active;     /**< 订阅者是否处于激活状态 */
} subscriber_entry_t;

/**
 * @brief 事件类型注册表条目
 * 
 * 每个已注册的事件类型对应一个此结构，包含该类型的所有订阅者信息。
 */
typedef struct {
    event_type_t       type;                  /**< 事件类型 ID */
    const char        *name;                  /**< 事件类型名称（用于调试） */
    subscriber_entry_t subscribers[CONFIG_EVENT_MAX_SUBSCRIBERS];  /**< 订阅者数组 */
    uint32_t           subscriber_count;      /**< 当前活跃的订阅者数量 */
    struct k_mutex     lock;                  /**< 保护订阅者列表的互斥锁 */
} event_type_entry_t;

/* =============================================================================
 * 核心 API (Core API)
 * 初始化和控制系统的主要接口
 * ============================================================================= */

/**
 * @brief 初始化事件系统
 * 
 * 调用此函数初始化事件系统的所有内部数据结构：
 * - 初始化事件类型表
 * - 初始化消息队列
 * - 初始化互斥锁
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 必须在调用其他事件系统 API 之前调用此函数
 * @note 此函数是幂等的，重复调用不会产生错误
 */
event_status_t event_system_init(void);

/**
 * @brief 启动事件系统投递
 * 
 * 将事件系统置为 running 状态，允许 event_publish/event_publish_from_isr 投递事件。
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 必须先调用 event_system_init()
 * @note 事件消费由 event_dispatcher 模块负责，本函数不会创建分发线程
 */
event_status_t event_system_start(void);

/**
 * @brief 停止事件系统投递
 * 
 * 将事件系统置为非 running 状态，并清空队列中的未处理事件。
 * 
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 停止后发布的事件将被拒绝
 * @note 已排队但未处理的动态事件负载会被释放
 */
event_status_t event_system_stop(void);

/**
 * @brief 检查事件系统是否正在运行
 * 
 * @return true 正在运行，false 已停止
 */
bool event_system_is_running(void);

/* =============================================================================
 * 事件类型管理 (Event Type Management)
 * 在发布/订阅事件前，必须先注册事件类型
 * ============================================================================= */

/**
 * @brief 注册新的事件类型
 * 
 * 注册一个事件类型，使其可以被订阅和发布。
 * 
 * @param type 事件类型 ID（0-255）
 * @param name 事件类型名称（用于调试和日志，建议唯一）
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 重复注册同一类型是幂等的，不会返回错误
 * @note 类型 ID 应预先规划，避免冲突
 */
event_status_t event_register_type(event_type_t type, const char *name);

/**
 * @brief 注销事件类型
 * 
 * 注销一个已注册的事件类型。
 * 
 * @param type 事件类型 ID
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 如果该类型仍有活跃订阅者，将返回错误
 * @note 注销后，该类型不能再被订阅或发布
 */
event_status_t event_unregister_type(event_type_t type);

/* =============================================================================
 * 订阅管理 (Subscription Management)
 * 订阅者通过订阅事件类型来接收通知
 * ============================================================================= */

/**
 * @brief 订阅事件类型
 * 
 * 注册一个回调函数来接收指定类型的事件通知。
 * 
 * @param type 事件类型 ID
 * @param callback 回调函数指针（不能为 NULL）
 * @param user_data 用户数据，将在回调时原样传入
 * @param subscriber_id 输出参数，接收分配的订阅者 ID
 * @return EVENT_OK 成功，其他错误码见 event_status_t（未注册类型返回 EVENT_ERR_NOT_FOUND）
 * 
 * @note 订阅者 ID 用于后续取消订阅
 * @note 每个订阅者最多订阅 CONFIG_EVENT_MAX_SUBSCRIBERS 个事件类型
 * @note 达到订阅上限时返回 EVENT_ERR_QUEUE_FULL
 */
event_status_t event_subscribe(event_type_t type,
                                event_callback_t callback,
                                void *user_data,
                                uint32_t *subscriber_id);

/**
 * @brief 取消订阅事件类型
 * 
 * 从指定事件类型中移除订阅者。
 * 
 * @param type 事件类型 ID
 * @param subscriber_id 要移除的订阅者 ID
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 取消订阅后，该订阅者不再接收此类型的事件
 * @note 如果订阅者 ID 不存在，返回 EVENT_ERR_NOT_FOUND
 */
event_status_t event_unsubscribe(event_type_t type, uint32_t subscriber_id);

/**
 * @brief 从所有事件类型中取消订阅
 * 
 * 遍历所有事件类型，移除指定订阅者 ID 的所有订阅。
 * 
 * @param subscriber_id 要移除的订阅者 ID
 * 
 * @note 用于模块卸载或清理时批量取消订阅
 * @note 此函数会锁定所有事件类型，可能耗时较长
 */
void event_unsubscribe_all(uint32_t subscriber_id);

/* =============================================================================
 * 事件发布 (Event Publishing)
 * 将事件发布到队列中，供订阅者消费
 * ============================================================================= */

/**
 * @brief 发布事件（同步方式）
 * 
 * 将事件发布到全局事件队列中。事件会被复制到队列中，
 * 然后由分发器线程异步处理。
 * 
 * @param event 指向要发布的事件的指针
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 如果队列已满，事件将被丢弃并返回 EVENT_ERR_QUEUE_FULL
 * @note event 指向的内容会被复制到队列，调用者可安全释放
 * @note 如果 event->is_dynamic 为 true，调用者仍需负责释放 event->data
 */
event_status_t event_publish(const event_t *event);

/**
 * @brief 从中断服务程序 (ISR) 发布事件
 * 
 * 专用于 ISR 上下文的事件发布函数。
 * 
 * @param event 指向要发布的事件的指针
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 此函数不使用互斥锁，适用于 ISR 上下文
 * @note 如果队列已满，事件将被丢弃
 * @note 避免在 ISR 中调用 event_publish_copy，因为其中包含 k_malloc
 * @note 与 event_publish 相同：event_system_stop 后 running 为假时返回 EVENT_ERR_INVALID_ARG
 */
event_status_t event_publish_from_isr(const event_t *event);

/**
 * @brief 发布事件并复制数据
 * 
 * 创建事件的内部副本，包括数据部分。
 * 适用于数据生命周期短于事件处理时间的场景。
 * 
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要复制的数据指针
 * @param data_len 数据长度（字节）
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 此函数会内部调用 k_malloc 分配内存
 * @note 数据副本在事件处理完成后由分发器自动释放
 * @note 适用于栈上数据或临时数据的发布
 */
event_status_t event_publish_copy(event_type_t type,
                                   event_priority_t priority,
                                   const void *data,
                                   size_t data_len);

/* =============================================================================
 * 事件创建与内存管理 (Event Creation & Memory Management)
 * 用于动态创建和管理事件对象
 * ============================================================================= */

/**
 * @brief 创建新事件
 * 
 * 分配并初始化一个事件对象。
 * 
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @return 指向新事件的指针，失败返回 NULL
 * 
 * @note 返回的事件 is_dynamic = true，必须用 event_free() 释放
 * @note 此函数只分配事件外壳，不分配数据空间
 * @note 使用 event_publish() 发布后，调用者仍需释放此事件对象
 */
event_t *event_create(event_type_t type, event_priority_t priority);

/**
 * @brief 创建带数据的事件
 * 
 * 分配一个事件对象并附加数据副本。
 * 
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要附加的数据指针
 * @param data_len 数据长度（字节）
 * @return 指向新事件的指针，失败返回 NULL
 * 
 * @note 如果 data 为 NULL 或 data_len 为 0，退化为 event_create()
 * @note 数据会被复制到新分配的内存中
 * @note 返回的事件必须用 event_free() 释放
 */
event_t *event_create_with_data(event_type_t type,
                                 event_priority_t priority,
                                 const void *data,
                                 size_t data_len);

/**
 * @brief 释放事件对象
 * 
 * 释放事件对象及其动态分配的数据。
 * 
 * @param event 要释放的事件指针
 * 
 * @note 如果 event->is_dynamic 为 true，event->data 也会被释放
 * @note 传入 NULL 是安全的，函数会直接返回
 * @note 不要释放栈上的事件对象
 */
void event_free(event_t *event);

/* =============================================================================
 * 工具函数 (Utility Functions)
 * 调试、监控和辅助功能
 * ============================================================================= */

/**
 * @brief 获取事件类型名称
 * 
 * @param type 事件类型 ID
 * @return 事件类型名称字符串
 * 
 * @note 如果类型未注册，返回 "UNREGISTERED"
 * @note 如果类型 ID 无效，返回 "UNKNOWN"
 */
const char *event_get_type_name(event_type_t type);

/**
 * @brief 获取事件类型的订阅者数量
 * 
 * @param type 事件类型 ID
 * @return 活跃订阅者数量
 * 
 * @note 此函数会获取互斥锁，避免在 ISR 中调用
 */
uint32_t event_get_subscriber_count(event_type_t type);

/**
 * @brief 获取事件系统统计信息
 * 
 * @param total_events 输出：已处理的事件总数
 * @param queue_depth 输出：当前队列深度（已用槽位数）
 * @param dropped_events 输出：被丢弃的事件数量
 * 
 * @note 用于系统监控和调试
 * @note 所有输出参数都可以为 NULL，表示不关心该值
 */
void event_get_statistics(uint32_t *total_events,
                          uint32_t *queue_depth,
                          uint32_t *dropped_events);

/**
 * @brief 获取全局事件队列指针
 * 
 * @return 指向全局事件队列的指针，未初始化时返回 NULL
 * 
 * @note 主要用于高级用法，如直接操作队列
 * @note 仅在 event_system_init() 调用后有效
 */
struct k_msgq *event_system_get_queue(void);

/**
 * @brief 将事件分发给所有订阅者
 * 
 * 遍历指定事件类型的所有活跃订阅者，调用其回调函数。
 * 
 * @param event 要分发的事件
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 * 
 * @note 此函数在分发器线程中调用
 * @note 回调函数执行时不持有事件类型锁，避免竞态条件
 * @note 如果无订阅者，返回 EVENT_ERR_NO_SUBSCRIBER
 */
event_status_t event_notify_subscribers(const event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SYSTEM_H */
