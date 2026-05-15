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
	if (ch == NULL || cfg == NULL) {
		return -EINVAL;
	}
	if (cfg->callback == NULL) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&ch->lock);

	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return -ESHUTDOWN;
	}

	if (ch->consumer_count >= CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		LOG_ERR("Channel '%s' consumer table full (max=%u)",
			ch->name, CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL);
		k_spin_unlock(&ch->lock, key);
		return -ENOMEM;
	}

	data_bus_consumer_t *consumer = &ch->consumers[ch->consumer_count];

	if (cfg->name != NULL) {
		(void)snprintf(consumer->name_storage, sizeof(consumer->name_storage), "%s", cfg->name);
	} else {
		consumer->name_storage[0] = '\0';
	}
	consumer->name = consumer->name_storage;
	consumer->manual_release = cfg->manual_release;
	consumer->callback = cfg->callback;
	consumer->user_data = cfg->user_data;
	consumer->last_seq = 0;
	consumer->active = true;

	ch->consumer_count++;

	k_spin_unlock(&ch->lock, key);

	LOG_INF("Consumer '%s' registered on '%s' (total=%u/%u)",
		consumer->name, ch->name, ch->consumer_count,
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

	/* 查找该消费者所属的通道 */
	/* 扫描所有通道以找到包含的通道 */
	/* 时间复杂度 O(N*M)，但通道和消费者通常很小 */

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
		LOG_WRN("Consumer unregister failed: not found");
		k_mutex_unlock(&g_channels_lock);
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&found_ch->lock);

	/* 标记为非活跃 */
	consumer->active = false;

	/* 通过移位压缩数组 */
	for (uint32_t i = found_idx; i < found_ch->consumer_count - 1; i++) {
		found_ch->consumers[i] = found_ch->consumers[i + 1];
		found_ch->consumers[i].name = found_ch->consumers[i].name_storage;
	}

	/* 清空最后一个槽 */
	if (found_ch->consumer_count > 0) {
		memset(&found_ch->consumers[found_ch->consumer_count - 1], 0,
		       sizeof(data_bus_consumer_t));
	}

	found_ch->consumer_count--;

	LOG_INF("Consumer '%s' unregistered from '%s' (remain=%u)",
		consumer->name, found_ch->name, found_ch->consumer_count);

	k_spin_unlock(&found_ch->lock, key);
	k_mutex_unlock(&g_channels_lock);

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

	k_spinlock_key_t key = k_spin_lock(&ch->lock);
	uint32_t count = ch->consumer_count;
	if (count > CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL) {
		count = CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL;
	}
	for (uint32_t i = 0; i < count; i++) {
		data_bus_consumer_t *c = &ch->consumers[i];

		snaps[i].active = c->active;
		snaps[i].manual_release = c->manual_release;
		snaps[i].callback = c->callback;
		snaps[i].user_data = c->user_data;
	}
	k_spin_unlock(&ch->lock, key);

	/* 统计活跃消费者以拆分引用 */
	uint32_t active_count = 0;
	for (uint32_t i = 0; i < count; i++) {
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
	LOG_DBG("Dispatch ch='%s' seq=%u ref+%u=%d", ch->name, block->seq,
		active_count, atomic_get(&block->ref_count));

	atomic_add(&block->ref_count, active_count);

	for (uint32_t i = 0; i < count; i++) {
		if (!snaps[i].active || snaps[i].callback == NULL) {
			continue;
		}

		LOG_DBG("  -> consumer[%u] manual_release=%d", i,
			snaps[i].manual_release);

		snaps[i].callback(ch, block, snaps[i].user_data);

		/* 框架自动释放隐式引用，除非消费者选择手动释放 */
		if (!snaps[i].manual_release) {
			data_bus_block_release(block);
		}

		k_spinlock_key_t lk = k_spin_lock(&ch->lock);
		if (i < ch->consumer_count &&
		    ch->consumers[i].callback == snaps[i].callback &&
		    ch->consumers[i].user_data == snaps[i].user_data) {
			ch->consumers[i].last_seq = block->seq;
		}
		k_spin_unlock(&ch->lock, lk);
	}
}
