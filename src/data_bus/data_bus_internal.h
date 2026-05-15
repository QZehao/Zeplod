/**
 * @file data_bus_internal.h
 * @brief Data Bus 内部全局状态声明
 *
 * data_bus.c、data_bus_channel.c、data_bus_consumer.c 共享。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：适配统一 auto_release 模型
 *
 */

#ifndef DATA_BUS_INTERNAL_H
#define DATA_BUS_INTERNAL_H

#include "data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 全局状态（定义在 data_bus.c 中）
 * ============================================================================ */

extern struct k_sem g_dispatcher_sem;
extern data_bus_channel_t *g_channels[CONFIG_DATA_BUS_MAX_CHANNELS];
extern uint32_t g_channel_count;
extern struct k_mutex g_channels_lock;
extern atomic_t g_initialized;
extern atomic_t g_shutting_down;

/* 分发线程（定义在 data_bus.c 中） */
extern struct k_thread g_dispatcher_thread_data;
extern k_thread_stack_t g_dispatcher_stack[];

/* ============================================================================
 * 事件桥接（可选，实现在 data_bus_event_bridge.c 中）
 * ============================================================================ */

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
void data_bus_event_bridge_notify(data_bus_channel_t *ch, uint32_t seq, size_t len);
#endif

/* ============================================================================
 * 通道池 slab（定义在 data_bus_channel.c 中）
 * ============================================================================ */

extern struct k_mem_slab data_bus_channel_slab;

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_INTERNAL_H */
