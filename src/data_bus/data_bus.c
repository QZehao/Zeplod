/**
 * @file data_bus.c
 * @brief Data Bus 核心 - 分发线程、初始化/反初始化、统计
 *
 * 分发线程从各通道队列中取出数据块，调用 data_bus_consumer_dispatch()
 * 将数据分发给所有注册的消费者。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：适配统一 auto_release 分发模型
 * 2026-05-15       2.1            zeh            分发线程：queue 非空时无消费者也出队并 release，避免块泄漏
 *
 */

#include "data_bus.h"
#include "data_bus_internal.h"
#include "data_bus_consumer.h"
#include "data_bus_memory.h"
#include "data_bus_channel.h"
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(data_bus, CONFIG_DATA_BUS_LOG_LEVEL);

/* ============================================================================
 * 全局状态
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
 * 分发线程
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

		/* 快照通道指针并固定每个（防止 destroy 释放 slab 与 UAF 竞争） */
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

			/* 批量出队：连续处理最多 8 个块，避免高负载通道饥饿 */
			for (int batch = 0; batch < 8; batch++) {
				data_bus_block_t *block = NULL;
				size_t len = 0;
				uint32_t consumer_count_snap = 0;

				k_spinlock_key_t key = k_spin_lock(&ch->lock);
				/* 只要队列有待投递块即出队；consumer_count==0 时也必须取出并 release，
				 * 否则 publish 已入队的块永不释放（且无消费者时无法通过回调释放）。 */
				if (atomic_get(&ch->active) && ch->queue_used > 0) {
					consumer_count_snap = ch->consumer_count;
					len = ring_buf_get(&ch->queue, (uint8_t *)&block, sizeof(block));
					if (len == sizeof(block) && block != NULL) {
						ch->queue_used--;
					}
				}
				k_spin_unlock(&ch->lock, key);

				if (len != sizeof(block) || block == NULL) {
					break;
				}

				LOG_DBG("dispatch ch='%s' seq=%u len=%zu consumers=%u",
					ch->name, block->seq, block->len, consumer_count_snap);

				/* 分发给所有消费者 */
				data_bus_consumer_dispatch(ch, block);

				/* 释放 bus 引用 */
				data_bus_block_release(block);
			}

			(void)atomic_dec(&ch->dispatch_hold);
		}
	}
}

/* ============================================================================
 * 公共 API: 生命周期
 * ============================================================================ */

int data_bus_init(void)
{
	if (atomic_get(&g_initialized)) {
		return 0; /* 已初始化 */
	}

	/* 初始化全局信号量 */
	k_sem_init(&g_dispatcher_sem, 0, K_SEM_MAX_LIMIT);

	/* 初始化通道表锁 */
	k_mutex_init(&g_channels_lock);

	/* 清空通道表 */
	memset(g_channels, 0, sizeof(g_channels));
	g_channel_count = 0;

	atomic_set(&g_shutting_down, 0);

	/* 创建分发线程 */
	k_thread_create(&g_dispatcher_thread_data,
			    g_dispatcher_stack,
			    K_THREAD_STACK_SIZEOF(g_dispatcher_stack),
			    data_bus_dispatcher_thread,
			    NULL, NULL, NULL,
			    CONFIG_DATA_BUS_DISPATCHER_PRIORITY,
			    0, K_NO_WAIT);

	k_thread_name_set(&g_dispatcher_thread_data, "data_bus_disp");

	atomic_set(&g_initialized, 1);
	LOG_INF("Data bus initialized (disp stack=%d prio=%d)",
		CONFIG_DATA_BUS_DISPATCHER_STACK_SIZE,
		CONFIG_DATA_BUS_DISPATCHER_PRIORITY);

	return 0;
}

int data_bus_deinit(void)
{
	if (!atomic_get(&g_initialized)) {
		return 0; /* 未初始化 */
	}

	/* 发出关闭信号 */
	atomic_set(&g_shutting_down, 1);

	/* 唤醒分发线程使其退出 */
	k_sem_give(&g_dispatcher_sem);

	/* 等待分发线程完成 */
	k_thread_join(&g_dispatcher_thread_data, K_FOREVER);

	/* 锁定通道表 */
	k_mutex_lock(&g_channels_lock, K_FOREVER);

	/* 销毁所有通道（排空队列，释放块） */
	while (g_channel_count > 0) {
		data_bus_channel_t *ch = g_channels[0];
		if (ch != NULL) {
			/* 排空队列并释放所有块 */
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

			/* 重置并释放通道对象 */
			data_bus_channel_obj_reset(ch);
			k_mem_slab_free(&data_bus_channel_slab, ch);
		}

		/* 从表中移除并压缩 */
		for (uint32_t j = 0; j < g_channel_count - 1; j++) {
			g_channels[j] = g_channels[j + 1];
		}
		g_channels[--g_channel_count] = NULL;
	}

	k_mutex_unlock(&g_channels_lock);

	atomic_set(&g_initialized, 0);
	LOG_INF("Data bus deinitialized");

	return 0;
}

/* ============================================================================
 * 公共 API: 统计
 * ============================================================================ */

void data_bus_channel_get_stats(const data_bus_channel_t *ch, data_bus_stats_t *stats)
{
	if (ch == NULL || stats == NULL) {
		return;
	}

	/* 移除 const 以进行锁访问 — 安全，因为我们只读取 */
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
 * 自动初始化
 * ============================================================================ */

static int data_bus_auto_init(void)
{
	return data_bus_init();
}

SYS_INIT(data_bus_auto_init, POST_KERNEL, APP_INIT_PRIO_DATA_BUS);
