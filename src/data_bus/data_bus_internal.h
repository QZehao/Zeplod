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

#include <zephyr/sys/ring_buffer.h>
#include "data_bus.h"
#include "zepl_thread_service.h"

#ifndef DATA_BUS_DISPATCHER_JOIN_TIMEOUT_MS
#define DATA_BUS_DISPATCHER_JOIN_TIMEOUT_MS ZEPL_THREAD_SERVICE_JOIN_TIMEOUT_MS
#endif

#ifndef DATA_BUS_DEINIT_DRAIN_TIMEOUT_MS
#define DATA_BUS_DEINIT_DRAIN_TIMEOUT_MS CONFIG_DATA_BUS_DEINIT_DRAIN_TIMEOUT_MS
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 内部对象布局（勿在应用代码中直接访问字段）
 * ============================================================================ */

struct data_bus_block {
    void*              ptr;
    size_t             len;
    atomic_t           ref_count;
    struct k_mem_slab* slab;
    uint32_t           seq;
    bool               malloc_fallback;
    bool               slab_exhausted;
    bool               memory_stats_accounted;
};

struct data_bus_consumer {
    data_bus_channel_t*   channel;
    const char*           name;
    char                  name_storage[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
    bool                  manual_release;
    data_bus_consume_fn_t callback;
    void*                 user_data;
    uint32_t              last_seq;
    uint32_t              generation;
    atomic_t              callback_hold;
    atomic_t              active;
};

struct data_bus_channel {
    const char*       name;
    char              name_storage[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
    struct ring_buf   queue;
    uint8_t           queue_buf[CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH * sizeof(void*)];
    struct k_spinlock lock;
    atomic_t          active;

    data_bus_consumer_t consumers[CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL];
    bool                consumer_slot_in_use[CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL];
    uint32_t            consumer_count;
    uint32_t            next_consumer_generation;

    uint32_t next_seq;
    uint32_t publish_count;
    uint32_t drop_count;
    uint32_t queue_full_count;
    uint32_t alloc_fail_count;
    uint32_t malloc_fallback_count;
    uint32_t slab_exhausted_count;
    uint32_t peak_queue_usage;
    uint32_t queue_used;
    atomic_t publish_hold;
    atomic_t dispatch_hold;
    atomic_t lifecycle_hold;
    atomic_t dispatch_ready;
};

/* ============================================================================
 * 全局状态（定义在 data_bus.c 中）
 * ============================================================================ */

extern struct k_sem        g_dispatcher_sem;
extern data_bus_channel_t* g_channels[CONFIG_DATA_BUS_MAX_CHANNELS];
extern uint32_t            g_channel_count;
extern struct k_mutex      g_channels_lock;
extern atomic_t            g_initialized;
extern atomic_t            g_shutting_down;

/**
 * @brief 要求 Data Bus 已初始化且未关闭
 * @return 0 就绪，-ENODEV 未初始化，-ESHUTDOWN 正在/已关闭
 */
int data_bus_require_initialized(void);

/* 分发线程（定义在 data_bus.c 中） */
extern struct k_thread  g_dispatcher_thread_data;
extern k_thread_stack_t g_dispatcher_stack[];

/* ============================================================================
 * 事件桥接（可选，实现在 data_bus_event_bridge.c 中）
 * ============================================================================ */

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
void data_bus_event_bridge_notify(data_bus_channel_t* ch, uint32_t seq, size_t len);
void data_bus_event_bridge_notify_memory_warning(data_bus_channel_t* ch, uint32_t malloc_fallback_count,
                                                 uint32_t slab_exhausted_count);
#endif

/* ============================================================================
 * 通道池 slab（定义在 data_bus_channel.c 中）
 * ============================================================================ */

extern struct k_mem_slab data_bus_channel_slab;

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_INTERNAL_H */
