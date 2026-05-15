/**
 * @file data_bus_consumer.c
 * @brief Data Bus consumer management - register/unregister/dispatch
 */

#include "data_bus_consumer.h"
#include "data_bus_internal.h"
#include "data_bus_memory.h"
#include <zephyr/kernel.h>
#include <string.h>

/* ============================================================================
 * Public API: consumer registration
 * ============================================================================ */

int data_bus_consumer_register(data_bus_channel_t *ch,
                                const data_bus_consumer_cfg_t *cfg,
                                data_bus_consumer_t **out_consumer)
{
	if (ch == NULL || cfg == NULL) {
		return -EINVAL;
	}
	if (cfg->callback == NULL) {
		return -EINVAL;
	}
	if (cfg->mode == DATA_BUS_MODE_COPY) {
		if (cfg->copy_buf == NULL || cfg->copy_buf_size == 0) {
			return -EINVAL;
		}
	}

	k_spinlock_key_t key = k_spin_lock(&ch->lock);

	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return -ESHUTDOWN;
	}

	if (ch->consumer_count >= CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		k_spin_unlock(&ch->lock, key);
		return -ENOMEM;
	}

	data_bus_consumer_t *consumer = &ch->consumers[ch->consumer_count];
	consumer->name = cfg->name;
	consumer->mode = cfg->mode;
	consumer->callback = cfg->callback;
	consumer->user_data = cfg->user_data;
	consumer->copy_buf = cfg->copy_buf;
	consumer->copy_buf_size = cfg->copy_buf_size;
	consumer->last_seq = 0;
	consumer->active = true;

	ch->consumer_count++;

	k_spin_unlock(&ch->lock, key);

	if (out_consumer != NULL) {
		*out_consumer = consumer;
	}

	return 0;
}

int data_bus_consumer_unregister(data_bus_consumer_t *consumer)
{
	if (consumer == NULL) {
		return -EINVAL;
	}

	/* Find which channel this consumer belongs to */
	/* We scan all channels to find the containing channel */
	/* This is O(N*M) but channels and consumers are typically small */

	k_mutex_lock(&g_channels_lock, K_FOREVER);

	data_bus_channel_t *found_ch = NULL;
	uint32_t found_idx = 0;

	for (uint32_t i = 0; i < g_channel_count; i++) {
		data_bus_channel_t *ch = g_channels[i];
		if (ch == NULL) {
			continue;
		}
		for (uint32_t j = 0; j < ch->consumer_count; j++) {
			if (&ch->consumers[j] == consumer) {
				found_ch = ch;
				found_idx = j;
				break;
			}
		}
		if (found_ch != NULL) {
			break;
		}
	}

	if (found_ch == NULL) {
		k_mutex_unlock(&g_channels_lock);
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&found_ch->lock);

	/* Mark inactive */
	consumer->active = false;

	/* Compact the array by shifting remaining consumers */
	for (uint32_t i = found_idx; i < found_ch->consumer_count - 1; i++) {
		found_ch->consumers[i] = found_ch->consumers[i + 1];
	}

	/* Clear the last slot */
	if (found_ch->consumer_count > 0) {
		memset(&found_ch->consumers[found_ch->consumer_count - 1], 0,
		       sizeof(data_bus_consumer_t));
	}

	found_ch->consumer_count--;

	k_spin_unlock(&found_ch->lock, key);
	k_mutex_unlock(&g_channels_lock);

	return 0;
}

/* ============================================================================
 * Internal: dispatch block to all consumers
 * ============================================================================ */

void data_bus_consumer_dispatch(data_bus_channel_t *ch, data_bus_block_t *block)
{
	if (ch == NULL || block == NULL) {
		return;
	}

	/* Snapshot consumer count under lock */
	k_spinlock_key_t key = k_spin_lock(&ch->lock);
	uint32_t count = ch->consumer_count;
	k_spin_unlock(&ch->lock, key);

	for (uint32_t i = 0; i < count; i++) {
		data_bus_consumer_t *consumer = &ch->consumers[i];

		if (!consumer->active || consumer->callback == NULL) {
			continue;
		}

		if (consumer->mode == DATA_BUS_MODE_REF) {
			/* REF mode: share the original block */
			data_bus_block_acquire(block);
			consumer->callback(ch, block, consumer->user_data);
			/* Consumer is responsible for calling release() */
		} else {
			/* COPY mode: copy data to consumer's buffer */
			data_bus_block_t copy;
			memset(&copy, 0, sizeof(copy));
			copy.ptr = consumer->copy_buf;
			copy.len = (block->len < consumer->copy_buf_size)
					   ? block->len
					   : consumer->copy_buf_size;
			memcpy(copy.ptr, block->ptr, copy.len);

			consumer->callback(ch, &copy, consumer->user_data);
			/* copy is on stack, no release needed */
		}

		/* Update last consumed sequence */
		consumer->last_seq = block->seq;
	}
}
