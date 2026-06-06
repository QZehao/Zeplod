/**
 * @file event_system_compat.h
 * @brief 标准事件系统应用适配层
 *
 * 保留应用层使用的初始化、停止和统计入口。商业事件系统支持已移除，
 * 事件注册、订阅、发布等操作直接使用 event_system.h 的标准 API。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-06
 */

#ifndef EVENT_SYSTEM_COMPAT_H
#define EVENT_SYSTEM_COMPAT_H

#include <stdbool.h>
#include <stdint.h>
#include "event_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 兼容保留的初始化配置
 *
 * 标准事件系统使用 Kconfig 配置，当前字段不会改变运行参数。
 * 保留该结构仅用于避免破坏已有应用调用。
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
 * @brief 事件系统统计信息
 *
 * 后三个历史扩展字段固定为 0，保留用于兼容已有 Shell 和应用代码。
 */
typedef struct {
    uint32_t total_events;
    uint32_t queue_depth;
    uint32_t dropped_events;
    uint32_t high_priority_processed;
    uint32_t batch_operations;
    uint32_t rate_limited_events;
} event_compat_stats_t;

int  event_compat_init(const event_compat_config_t* config);
int  event_compat_start(void);
int  event_compat_stop(void);
bool event_compat_is_running(void);
int  event_compat_shutdown(void);
void event_compat_get_statistics(event_compat_stats_t* stats);
void event_compat_reset_statistics(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_SYSTEM_COMPAT_H */
