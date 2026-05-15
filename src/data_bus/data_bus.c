/**
 * @file data_bus.c
 * @brief Data Bus core - dispatcher thread, init/deinit, stats
 */

#include "data_bus.h"
#include "data_bus_internal.h"
#include "data_bus_consumer.h"
#include "data_bus_memory.h"
#include "data_bus_channel.h"
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <string.h>

/* ============================================================================
 * Global state
 * ============================================================================ */

struct k_sem g_dispatcher_sem;
data_bus_channel_t *g_channels[CONFIG_DATA_BUS_MAX_CHANNELS];
uint32_t g_channel_count;
struct k_mutex g_channels_lock;
atomic_t g_initialized;
atomic_t g_shutting_down;

struct k_thread g_dispatcher_thread_data;
K_THREAD_STACK_DEFINE(g_dispatcher_stack, CONFIG_DATA_BUS_DISPATCHER_STACK_SIZE);

/* ============================================================================
 * Dispatcher thread
 * ============================================================================ */

static void data_bus_dispatcher_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		if (atomic_get(&g_shutting_down)) {
			break;
		}

		k_sem_take(&g_dispatcher_sem, K_FOREVER);

		if (atomic_get(&g_shutting_down)) {
			break;
		}

		/* Snapshot channel pointers and pin each (prevents destroy slab-free vs UAF) */
		data_bus_channel_t *snap[CONFIG_DATA_BUS_MAX_CHANNELS];

		k_mutex_lock(&g_channels_lock, K_FOREVER);
		uint32_t n = g_channel_count;
		for (uint32_t i = 0; i < n; i++) {
			snap[i] = g_channels[i];
			if (snap[i] != NULL) {
				(void)atomic_inc(&snap[i]->dispatch_hold);
			}
		}
		k_mutex_unlock(&g_channels_lock);

		for (uint32_t i = 0; i < n; i++) {
			data_bus_channel_t *ch = snap[i];

			if (ch == NULL) {
				continue;
			}

			data_bus_block_t *block = NULL;
			size_t len = 0;

			k_spinlock_key_t key = k_spin_lock(&ch->lock);
			if (ch->active) {
				len = ring_buf_get(&ch->queue, (uint8_t *)&block, sizeof(block));
				if (len == sizeof(block) && block != NULL) {
					ch->queue_used--;
				}
			}
			k_spin_unlock(&ch->lock, key);

			if (len == sizeof(block) && block != NULL) {
				/* Dispatch to all consumers */
				data_bus_consumer_dispatch(ch, block);

				/* Release bus reference */
				data_bus_block_release(block);
			}

			k_mutex_lock(&g_channels_lock, K_FOREVER);
			(void)atomic_dec(&ch->dispatch_hold);
			k_mutex_unlock(&g_channels_lock);
		}
	}
}

/* ============================================================================
 * Public API: lifecycle
 * ============================================================================ */

int data_bus_init(void)
{
	if (atomic_get(&g_initialized)) {
		return 0; /* Already initialized */
	}

	/* Initialize global semaphore */
	k_sem_init(&g_dispatcher_sem, 0, K_SEM_MAX_LIMIT);

	/* Initialize channel table lock */
	k_mutex_init(&g_channels_lock);

	/* Clear channel table */
	memset(g_channels, 0, sizeof(g_channels));
	g_channel_count = 0;

	atomic_set(&g_shutting_down, 0);

	/* Create dispatcher thread */
	k_thread_create(&g_dispatcher_thread_data,
			    g_dispatcher_stack,
			    K_THREAD_STACK_SIZEOF(g_dispatcher_stack),
			    data_bus_dispatcher_thread,
			    NULL, NULL, NULL,
			    CONFIG_DATA_BUS_DISPATCHER_PRIORITY,
			    0, K_NO_WAIT);

	k_thread_name_set(&g_dispatcher_thread_data, "data_bus_disp");

	atomic_set(&g_initialized, 1);

	return 0;
}

int data_bus_deinit(void)
{
	if (!atomic_get(&g_initialized)) {
		return 0; /* Not initialized */
	}

	/* Signal shutdown */
	atomic_set(&g_shutting_down, 1);

	/* Wake up dispatcher thread so it can exit */
	k_sem_give(&g_dispatcher_sem);

	/* Wait for dispatcher thread to finish */
	k_thread_join(&g_dispatcher_thread_data, K_FOREVER);

	/* Lock channel table */
	k_mutex_lock(&g_channels_lock, K_FOREVER);

	/* Destroy all channels (drain queues, release blocks) */
	while (g_channel_count > 0) {
		data_bus_channel_t *ch = g_channels[0];
		if (ch != NULL) {
			/* Unregister all consumers */
			for (uint32_t i = 0; i < ch->consumer_count; i++) {
				ch->consumers[i].active = false;
			}
			ch->consumer_count = 0;

			/* Drain queue and release all blocks */
			data_bus_block_t *block = NULL;
			while (true) {
				k_spinlock_key_t key = k_spin_lock(&ch->lock);
				size_t len = ring_buf_get(&ch->queue, (uint8_t *)&block, sizeof(block));
				if (len == sizeof(block) && block != NULL) {
					ch->queue_used--;
				}
				k_spin_unlock(&ch->lock, key);

				if (len != sizeof(block) || block == NULL) {
					break;
				}
				data_bus_block_release(block);
			}

			/* Reset and free channel object */
			data_bus_channel_obj_reset(ch);
			k_mem_slab_free(&data_bus_channel_slab, ch);
		}

		/* Remove from table and compact */
		for (uint32_t j = 0; j < g_channel_count - 1; j++) {
			g_channels[j] = g_channels[j + 1];
		}
		g_channels[--g_channel_count] = NULL;
	}

	k_mutex_unlock(&g_channels_lock);

	atomic_set(&g_initialized, 0);

	return 0;
}

/* ============================================================================
 * Public API: statistics
 * ============================================================================ */

void data_bus_channel_get_stats(const data_bus_channel_t *ch, data_bus_stats_t *stats)
{
	if (ch == NULL || stats == NULL) {
		return;
	}

	/* Cast away const for lock access - safe because we only read */
	data_bus_channel_t *ch_rw = (data_bus_channel_t *)ch;
	k_spinlock_key_t key = k_spin_lock(&ch_rw->lock);
	stats->publish_count = ch->publish_count;
	stats->drop_count = ch->drop_count;
	stats->queue_full_count = ch->queue_full_count;
	stats->alloc_fail_count = ch->alloc_fail_count;
	stats->consumer_count = ch->consumer_count;
	stats->peak_queue_usage = ch->peak_queue_usage;
	k_spin_unlock(&ch_rw->lock, key);
}

void data_bus_reset_stats(data_bus_channel_t *ch)
{
	if (ch == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&ch->lock);
	ch->publish_count = 0;
	ch->drop_count = 0;
	ch->queue_full_count = 0;
	ch->alloc_fail_count = 0;
	ch->peak_queue_usage = 0;
	k_spin_unlock(&ch->lock, key);
}

/* ============================================================================
 * Auto initialization
 * ============================================================================ */

static int data_bus_auto_init(void)
{
	return data_bus_init();
}

SYS_INIT(data_bus_auto_init, POST_KERNEL, APP_INIT_PRIO_DATA_BUS);
