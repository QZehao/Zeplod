/**
 * @file event_system_compat.h
 * @brief 事件系统兼容层 - 标准版与商业版统一接口
 *
 * 提供事件系统的抽象层，使得应用代码可以在标准版和商业版之间无缝切换。
 *
 * 使用方式：
 * - 标准版：默认使用，无需额外配置
 * - 商业版：在 prj.conf 中设置 CONFIG_USE_EVENT_SYSTEM_PRO=y
 *
 * Pro 模式 API 覆盖范围：
 * - 已映射：register/subscribe/publish/publish_from_isr/publish_copy 及查询类宏（见下方 #if EVENT_COMPAT_USE_PRO）
 * - 未映射：event_create / event_create_with_data / event_create_rt / event_create_with_data_rt /
 *   event_create_from_isr / event_free / event_free_data（商业版若提供对应 API，需在此补充 #define）
 * - 迁移建议：Pro 工程优先使用 event_publish_copy 等发布路径；勿直接依赖未映射的堆式 create/free API
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 *
 */

#ifndef EVENT_SYSTEM_COMPAT_H
#define EVENT_SYSTEM_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置检测
 * ============================================================================= */

/**
 * @brief 检测是否使用商业版事件系统
 * @note 用户只需在 prj.conf 中定义 CONFIG_USE_EVENT_SYSTEM_PRO=y
 */
#if defined(CONFIG_USE_EVENT_SYSTEM_PRO) && defined(CONFIG_EVENT_SYSTEM_PRO)
#define EVENT_COMPAT_USE_PRO 1
#else
#define EVENT_COMPAT_USE_PRO 0
#endif

/* =============================================================================
 * 前向声明
 * ============================================================================= */

/**
 * @brief 事件系统配置（商业版兼容）
 */
typedef struct {
    uint16_t high_priority_queue_size;
    uint16_t normal_priority_queue_size;
    uint16_t low_priority_queue_size;
    bool     enable_playback;
    bool     enable_statistics;
    bool     enable_rate_limit;
    bool     enable_batch;
    bool     enable_persist;
    bool     enable_profiling;
    bool     enable_security;
} event_compat_config_t;

/**
 * @brief 事件系统统计信息（统一格式）
 */
typedef struct {
    uint32_t total_events;
    uint32_t queue_depth;
    uint32_t dropped_events;
    /* 商业版扩展统计 */
    uint32_t high_priority_processed;
    uint32_t batch_operations;
    uint32_t rate_limited_events;
} event_compat_stats_t;

/* =============================================================================
 * 统一初始化接口
 * ============================================================================= */

/**
 * @brief 初始化事件系统（统一入口）
 *
 * 根据配置自动选择标准版或商业版初始化。
 *
 * @param config 商业版配置（标准版忽略，传 NULL 即可）
 * @return 0 成功，负值错误码
 */
int event_compat_init(const event_compat_config_t* config);

/**
 * @brief 启动事件系统（统一入口）
 * @return 0 成功，负值错误码
 */
int event_compat_start(void);

/**
 * @brief 停止事件系统投递（统一入口）
 *
 * 标准版会停止分发器线程并清空队列；再次 event_compat_start() 时恢复分发器。
 *
 * @return 0 成功，负值错误码
 */
int event_compat_stop(void);

/**
 * @brief 检查事件系统是否正在运行
 * @return true 运行中，false 已停止
 */
bool event_compat_is_running(void);

/**
 * @brief 关闭事件系统并释放资源
 * @return 0 成功，负值错误码
 */
int event_compat_shutdown(void);

/* =============================================================================
 * 统计接口
 * ============================================================================= */

/**
 * @brief 获取事件系统统计信息（统一格式）
 *
 * 标准版：填充基础统计
 * 商业版：填充基础统计 + 扩展统计
 *
 * @param stats 输出统计信息
 */
void event_compat_get_statistics(event_compat_stats_t* stats);

/**
 * @brief 重置统计信息
 */
void event_compat_reset_statistics(void);

/* =============================================================================
 * 宏定义：API 兼容映射
 * ============================================================================= */

#if EVENT_COMPAT_USE_PRO

/* ========================================
 * 商业版：直接映射到 event_system_pro API
 * ======================================== */

/**
 * @brief 注册事件类型（商业版）
 * @note 商业版 API 保持不变
 */
#define event_register_type(type, name) event_system_pro_register_type(type, name)

/**
 * @brief 订阅事件（商业版）
 */
#define event_subscribe(type, callback, user_data, subscriber_id)                                                      \
    event_system_pro_subscribe(type, callback, user_data, subscriber_id)

/**
 * @brief 取消订阅（商业版）
 */
#define event_unsubscribe(type, subscriber_id)             event_system_pro_unsubscribe(type, subscriber_id)

/**
 * @brief 取消全部订阅（商业版）
 */
#define event_unsubscribe_all(subscriber_id)               event_system_pro_unsubscribe_all(subscriber_id)

/**
 * @brief 发布事件（商业版）
 */
#define event_publish(event)                               event_system_pro_publish(event)

/**
 * @brief 从 ISR 发布事件（商业版）
 */
#define event_publish_from_isr(event)                      event_system_pro_publish_from_isr(event)

/**
 * @brief 发布事件并复制数据（商业版）
 */
#define event_publish_copy(type, priority, data, data_len) event_system_pro_publish_copy(type, priority, data, data_len)

/**
 * @brief 获取事件类型名称（商业版）
 */
#define event_get_type_name(type)                          event_system_pro_get_type_name(type)

/**
 * @brief 获取事件类型订阅者数量（商业版）
 */
#define event_get_subscriber_count(type)                   event_system_pro_get_subscriber_count(type)

/**
 * @brief 获取事件队列指针（商业版）
 */
#define event_system_get_queue()                           event_system_pro_get_queue()

#else

/* ========================================
 * 标准版：直接使用原生 API（不做重映射，避免递归）
 * event_register_type, event_subscribe 等直接调用原生函数
 * ======================================== */

/* 标准版下，所有 event_* 函数直接使用 event_system.h 中的原生定义 */

#endif /* EVENT_COMPAT_USE_PRO */

/* =============================================================================
 * 商业版 API 前置声明（用于编译检测）
 * ============================================================================= */

#if EVENT_COMPAT_USE_PRO
#include "event_system_pro.h"
#else
#include "event_system.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SYSTEM_COMPAT_H */
