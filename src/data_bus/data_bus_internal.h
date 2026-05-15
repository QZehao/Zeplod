/**
 * @file data_bus_internal.h
 * @brief Data Bus internal global state declarations
 *
 * Shared between data_bus.c, data_bus_channel.c, data_bus_consumer.c
 */

#ifndef DATA_BUS_INTERNAL_H
#define DATA_BUS_INTERNAL_H

#include "data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Global state (defined in data_bus.c)
 * ============================================================================ */

extern struct k_sem g_dispatcher_sem;
extern data_bus_channel_t *g_channels[CONFIG_DATA_BUS_MAX_CHANNELS];
extern uint32_t g_channel_count;
extern struct k_mutex g_channels_lock;
extern atomic_t g_initialized;
extern atomic_t g_shutting_down;

/* Dispatcher thread (defined in data_bus.c) */
extern struct k_thread g_dispatcher_thread_data;
extern k_thread_stack_t g_dispatcher_stack[];

/* ============================================================================
 * Event bridge (optional, implemented in data_bus_event_bridge.c)
 * ============================================================================ */

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
void data_bus_event_bridge_notify(data_bus_channel_t *ch, uint32_t seq, size_t len);
#endif

/* ============================================================================
 * Channel pool slab (defined in data_bus_channel.c)
 * ============================================================================ */

extern struct k_mem_slab data_bus_channel_slab;

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_INTERNAL_H */
