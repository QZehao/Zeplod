/**
 * @file data_bus_consumer.c
 * @brief Data Bus 消费者管理 - 注册/注销/分发
 *
 * 分发核心逻辑：
 * 1. 快照活跃消费者列表
 * 2. atomic_add(ref_count, active_count) 拆分引用
 * 3. 逐个调用消费者回调
 * 4. 非 manual_release 模式下，回调后框架自动 release
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：删除 COPY 模式，实现 +N 引用拆分
 * 2026-05-20       2.1            zeh            消费者固定槽位：注销不压缩数组，保证 out_consumer 地址稳定
 *
 */

#include "data_bus_consumer.h"
#include "data_bus_internal.h"
#include "data_bus_memory.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(data_bus_consumer, CONFIG_DATA_BUS_LOG_LEVEL);

typedef struct {
	data_bus_consumer_t *consumer; /* 消费者对象指针（固定槽位地址） */
	bool active;
	bool manual_release;
	data_bus_consume_fn_t callback;
	void *user_data;
} data_bus_consumer_snap_t;

/* ============================================================================
 * 公共 API: 消费者注册
 * ============================================================================ */

int data_bus_consumer_register(data_bus_channel_t *ch,
			       const data_bus_consumer_cfg_t *cfg,
			       data_bus_consumer_t **out_consumer)
{
	int ready = data_bus_require_initialized();

	if (ready != 0) {
		return ready;
	}
	if (ch == NULL || cfg == NULL) {
		return -EINVAL;
	}
	if (cfg->callback == NULL) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&ch->lock);

	if (!atomic_get(&ch->active)) {
		k_spin_unlock(&ch->lock, key);
		return -ESHUTDOWN;
	}

	if (ch->consumer_count >= CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		LOG_ERR("Channel '%s' consumer table full (max=%u)", ch->name,
			CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL);
		k_spin_unlock(&ch->lock, key);
		return -ENOMEM;
	}

	uint32_t slot = CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL;
	for (uint32_t i = 0; i < CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL; i++) {
		if (!ch->consumer_slot_in_use[i]) {
			slot = i;
			break;
		}
	}
	if (slot >= CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		k_spin_unlock(&ch->lock, key);
		return -ENOMEM;
	}

	data_bus_consumer_t *consumer = &ch->consumers[slot];

	memset(consumer, 0, sizeof(*consumer));

	if (cfg->name != NULL) {
		int name_ret = snprintf(consumer->name_storage, sizeof(consumer->name_storage), "%s",
					cfg->name);

		if (name_ret < 0 || (size_t)name_ret >= sizeof(consumer->name_storage)) {
			k_spin_unlock(&ch->lock, key);
			return -EINVAL;
		}
	} else {
		consumer->name_storage[0] = '\0';
	}
	consumer->name = consumer->name_storage;
	consumer->channel = ch;
	consumer->manual_release = cfg->manual_release;
	consumer->callback = cfg->callback;
	consumer->user_data = cfg->user_data;
	consumer->last_seq = 0;
	atomic_set(&consumer->active, 1);

	ch->consumer_slot_in_use[slot] = true;
	ch->consumer_count++;

	uint32_t total = ch->consumer_count;

	k_spin_unlock(&ch->lock, key);

	LOG_INF("Consumer '%s' registered on '%s' (total=%u/%u)", consumer->name, ch->name, total,
		CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL);

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

	data_bus_channel_t *found_ch = consumer->channel;

	if (found_ch == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&g_channels_lock, K_FOREVER);

	if (!atomic_get(&found_ch->active)) {
		k_mutex_unlock(&g_channels_lock);
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&found_ch->lock);

	uint32_t found_idx = CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL;
	for (uint32_t j = 0; j < CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL; j++) {
		if (found_ch->consumer_slot_in_use[j] && &found_ch->consumers[j] == consumer) {
			found_idx = j;
			break;
		}
	}

	if (found_idx >= CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		k_spin_unlock(&found_ch->lock, key);
		k_mutex_unlock(&g_channels_lock);
		LOG_WRN("Consumer unregister failed: not found on channel '%s'", found_ch->name);
		return -EINVAL;
	}

	char log_name[sizeof(consumer->name_storage)];
	(void)snprintf(log_name, sizeof(log_name), "%s", consumer->name_storage);

	atomic_set(&consumer->active, 0);
	consumer->channel = NULL;
	memset(consumer, 0, sizeof(*consumer));
	found_ch->consumer_slot_in_use[found_idx] = false;
	found_ch->consumer_count--;

	uint32_t remain = found_ch->consumer_count;

	k_spin_unlock(&found_ch->lock, key);
	k_mutex_unlock(&g_channels_lock);

	LOG_INF("Consumer '%s' unregistered from '%s' (remain=%u)", log_name, found_ch->name, remain);

	return 0;
}

/* ============================================================================
 * 内部：将块分发给所有消费者
 * ============================================================================ */

void data_bus_consumer_dispatch(data_bus_channel_t *ch, data_bus_block_t *block)
{
	if (ch == NULL || block == NULL) {
		return;
	}

	data_bus_consumer_snap_t snaps[CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL];
	uint32_t snap_count = 0;

	k_spinlock_key_t key = k_spin_lock(&ch->lock);
	for (uint32_t i = 0; i < CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL; i++) {
		if (!ch->consumer_slot_in_use[i]) {
			continue;
		}

		data_bus_consumer_t *c = &ch->consumers[i];

		snaps[snap_count].consumer = c;
		snaps[snap_count].active = atomic_get(&c->active);
		snaps[snap_count].manual_release = c->manual_release;
		snaps[snap_count].callback = c->callback;
		snaps[snap_count].user_data = c->user_data;
		snap_count++;
	}
	k_spin_unlock(&ch->lock, key);

	/* 统计活跃消费者以拆分引用 */
	uint32_t active_count = 0;
	for (uint32_t i = 0; i < snap_count; i++) {
		if (snaps[i].active && snaps[i].callback != NULL) {
			active_count++;
		}
	}

	if (active_count == 0) {
		return;
	}

	/*
	 * 拆分 bus 引用：ref_count 原为 1（bus 持有）。
	 * 增加 active_count 使总引用数 = 1 + active_count。
	 * 每个消费者获得一份隐式引用。
	 */
	LOG_DBG("Dispatch ch='%s' seq=%u ref+%u=%d", ch->name, block->seq, active_count,
		(int)atomic_get(&block->ref_count));

	atomic_add(&block->ref_count, active_count);

	for (uint32_t i = 0; i < snap_count; i++) {
		if (!snaps[i].active || snaps[i].callback == NULL) {
			continue;
		}

		LOG_DBG("  -> consumer manual_release=%d", snaps[i].manual_release);

		snaps[i].callback(ch, block, snaps[i].user_data);

		/* 框架自动释放隐式引用，除非消费者选择手动释放 */
		if (!snaps[i].manual_release) {
			data_bus_block_release(block);
		}

		k_spinlock_key_t lk = k_spin_lock(&ch->lock);
		data_bus_consumer_t *target = snaps[i].consumer;
		if (target != NULL && atomic_get(&target->active) &&
		    target->callback == snaps[i].callback && target->user_data == snaps[i].user_data) {
			target->last_seq = block->seq;
		}
		k_spin_unlock(&ch->lock, lk);
	}
}
