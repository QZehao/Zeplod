/**
 * @file data_bus_channel.c
 * @brief Data Bus channel management - create/destroy/find/publish
 */

#include "data_bus_channel.h"
#include "data_bus_internal.h"
#include "data_bus_memory.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Channel object slab
 * ============================================================================ */

K_MEM_SLAB_DEFINE(data_bus_channel_slab, sizeof(data_bus_channel_t),
                  CONFIG_DATA_BUS_MAX_CHANNELS, sizeof(void *));

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

static bool name_valid(const char *name)
{
	if (name == NULL) {
		return false;
	}
	size_t len = strlen(name);
	if (len == 0 || len >= CONFIG_DATA_BUS_CHANNEL_NAME_MAX) {
		return false;
	}
	return true;
}

static data_bus_channel_t *find_in_table(const char *name)
{
	for (uint32_t i = 0; i < g_channel_count; i++) {
		data_bus_channel_t *ch = g_channels[i];
		if (ch != NULL && ch->active && strcmp(ch->name, name) == 0) {
			return ch;
		}
	}
	return NULL;
}

/* ============================================================================
 * Channel object init/reset
 * ============================================================================ */

int data_bus_channel_obj_init(data_bus_channel_t *ch, const char *name)
{
	__ASSERT(ch != NULL, "NULL channel pointer");
	__ASSERT(name != NULL, "NULL name");

	memset(ch, 0, sizeof(*ch));

	{
		int name_ret = snprintf(ch->name_storage, sizeof(ch->name_storage), "%s", name);

		if (name_ret < 0 || (size_t)name_ret >= sizeof(ch->name_storage)) {
			return -EINVAL;
		}
	}

	ch->name = ch->name_storage;
	ring_buf_init(&ch->queue, sizeof(ch->queue_buf), ch->queue_buf);
	/* k_spinlock zero-initialized is valid */
	ch->active = true;
	ch->next_seq = 0;
	atomic_set(&ch->dispatch_hold, 0);

	return 0;
}

void data_bus_channel_obj_reset(data_bus_channel_t *ch)
{
	if (ch == NULL) {
		return;
	}

	/* Drain any pending blocks */
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

	/* Unregister all consumers */
	for (uint32_t i = 0; i < ch->consumer_count; i++) {
		ch->consumers[i].active = false;
	}
	ch->consumer_count = 0;

	ch->active = false;
}

/* ============================================================================
 * Public API: channel management
 * ============================================================================ */

int data_bus_channel_create(const char *name, data_bus_channel_t **out_channel)
{
	if (out_channel == NULL) {
		return -EINVAL;
	}
	if (!name_valid(name)) {
		return -EINVAL;
	}

	k_mutex_lock(&g_channels_lock, K_FOREVER);

	/* Check for duplicate name */
	if (find_in_table(name) != NULL) {
		k_mutex_unlock(&g_channels_lock);
		return -EEXIST;
	}

	/* Find a free slot in the table */
	if (g_channel_count >= CONFIG_DATA_BUS_MAX_CHANNELS) {
		k_mutex_unlock(&g_channels_lock);
		return -ENOMEM;
	}

	/* Allocate channel object from slab */
	data_bus_channel_t *ch = NULL;
	int ret = k_mem_slab_alloc(&data_bus_channel_slab, (void **)&ch, K_NO_WAIT);
	if (ret != 0) {
		k_mutex_unlock(&g_channels_lock);
		return -ENOMEM;
	}

	/* Initialize the channel */
	ret = data_bus_channel_obj_init(ch, name);
	if (ret != 0) {
		k_mem_slab_free(&data_bus_channel_slab, ch);
		k_mutex_unlock(&g_channels_lock);
		return ret;
	}

	/* Add to global table */
	g_channels[g_channel_count++] = ch;

	k_mutex_unlock(&g_channels_lock);

	*out_channel = ch;
	return 0;
}

int data_bus_channel_destroy(data_bus_channel_t *ch)
{
	if (ch == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&g_channels_lock, K_FOREVER);

	if (!ch->active) {
		k_mutex_unlock(&g_channels_lock);
		return -EINVAL;
	}

	/* Check for active consumers */
	for (uint32_t i = 0; i < ch->consumer_count; i++) {
		if (ch->consumers[i].active) {
			k_mutex_unlock(&g_channels_lock);
			return -EBUSY;
		}
	}

	/* Check for pending data */
	k_spinlock_key_t key = k_spin_lock(&ch->lock);
	bool queue_empty = (ch->queue_used == 0);
	k_spin_unlock(&ch->lock, key);

	if (!queue_empty) {
		k_mutex_unlock(&g_channels_lock);
		return -EAGAIN;
	}

	if (atomic_get(&ch->dispatch_hold) != 0) {
		k_mutex_unlock(&g_channels_lock);
		return -EAGAIN;
	}

	/* Remove from table */
	for (uint32_t i = 0; i < g_channel_count; i++) {
		if (g_channels[i] == ch) {
			/* Compact the table by shifting */
			for (uint32_t j = i; j < g_channel_count - 1; j++) {
				g_channels[j] = g_channels[j + 1];
			}
			g_channels[--g_channel_count] = NULL;
			break;
		}
	}

	/* Reset and free */
	data_bus_channel_obj_reset(ch);
	k_mem_slab_free(&data_bus_channel_slab, ch);

	k_mutex_unlock(&g_channels_lock);
	return 0;
}

data_bus_channel_t *data_bus_channel_find(const char *name)
{
	if (name == NULL) {
		return NULL;
	}

	k_mutex_lock(&g_channels_lock, K_FOREVER);
	data_bus_channel_t *ch = find_in_table(name);
	k_mutex_unlock(&g_channels_lock);

	return ch;
}

/* ============================================================================
 * Public API: publishing
 * ============================================================================ */

int data_bus_publish(data_bus_channel_t *ch, const void *data, size_t len)
{
	if (ch == NULL || (data == NULL && len > 0)) {
		return -EINVAL;
	}
	if (len == 0) {
		return -EINVAL;
	}

	/* Check shutdown state */
	if (atomic_get(&g_shutting_down)) {
		return -ESHUTDOWN;
	}

	k_spinlock_key_t skey = k_spin_lock(&ch->lock);
	bool active = ch->active;
	k_spin_unlock(&ch->lock, skey);

	if (!active) {
		return -ESHUTDOWN;
	}

	bool in_isr = k_is_in_isr();
	data_bus_block_t *block = NULL;

	/* Allocate block */
	if (in_isr) {
		block = data_bus_mem_alloc_isr(len);
	} else {
		block = data_bus_mem_alloc(len);
	}
	if (block == NULL) {
		k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
		ch->alloc_fail_count++;
		k_spin_unlock(&ch->lock, fkey);
		return -ENOMEM;
	}

	/* Copy data */
	memcpy(block->ptr, data, len);
	/* block->len already set by mem_alloc */
	atomic_set(&block->ref_count, 0);

	/* Enqueue */
	skey = k_spin_lock(&ch->lock);
	int ret = ring_buf_put(&ch->queue, (uint8_t *)&block, sizeof(block));
	if (ret == sizeof(block)) {
		block->seq = ch->next_seq++;
		ch->publish_count++;
		atomic_set(&block->ref_count, 1);
		ch->queue_used++;
		if (ch->queue_used > ch->peak_queue_usage) {
			ch->peak_queue_usage = ch->queue_used;
		}
	}
	k_spin_unlock(&ch->lock, skey);

	if (ret != sizeof(block)) {
		data_bus_mem_free(block);
		k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
		ch->drop_count++;
		ch->queue_full_count++;
		k_spin_unlock(&ch->lock, fkey);
		return -ENOBUFS;
	}

	/* Signal dispatcher thread */
	k_sem_give(&g_dispatcher_sem);

	/* Event bridge (thread path only) */
#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
	if (!in_isr) {
		data_bus_event_bridge_notify(ch, block->seq, block->len);
	}
#endif

	return 0;
}

int data_bus_publish_block(data_bus_channel_t *ch, data_bus_block_t *block)
{
	if (ch == NULL || block == NULL) {
		return -EINVAL;
	}
	if (block->ptr == NULL || block->len == 0) {
		return -EINVAL;
	}
	if (atomic_get(&g_shutting_down)) {
		return -ESHUTDOWN;
	}

	k_spinlock_key_t skey = k_spin_lock(&ch->lock);
	bool active = ch->active;
	k_spin_unlock(&ch->lock, skey);

	if (!active) {
		return -ESHUTDOWN;
	}

	/* Enqueue */
	skey = k_spin_lock(&ch->lock);
	int ret = ring_buf_put(&ch->queue, (uint8_t *)&block, sizeof(block));
	if (ret == sizeof(block)) {
		block->seq = ch->next_seq++;
		ch->publish_count++;
		atomic_set(&block->ref_count, 1);
		ch->queue_used++;
		if (ch->queue_used > ch->peak_queue_usage) {
			ch->peak_queue_usage = ch->queue_used;
		}
	}
	k_spin_unlock(&ch->lock, skey);

	if (ret != sizeof(block)) {
		k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
		ch->drop_count++;
		ch->queue_full_count++;
		k_spin_unlock(&ch->lock, fkey);
		return -ENOBUFS;
	}

	/* Signal dispatcher */
	k_sem_give(&g_dispatcher_sem);

	return 0;
}
