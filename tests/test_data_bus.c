/**
 * @file test_data_bus.c
 * @brief Data Bus 单元测试
 *
 * 覆盖：初始化/反初始化、通道管理、自动释放、多消费者、
 * retain 异步持有、队列溢出、消费者注销、publish_block、
 * manual_release、ISR 发布、统计重置。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：删除 COPY 模式测试，添加 retain 测试
 * 2026-05-15       2.1            zeh            无消费者时队列排空回归测试
 *
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "data_bus.h"
#include "data_bus_memory.h"

LOG_MODULE_REGISTER(test_data_bus);

/* ============================================================================
 * 测试夹具
 * ============================================================================ */

static struct k_sem g_test_sem;
static atomic_t g_call_count;
static uint32_t g_recv_seq = 0;
static data_bus_block_t *g_retained_block = NULL;

/* ============================================================================
 * 消费者回调
 * ============================================================================ */

static void auto_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *user_data)
{
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	atomic_inc(&g_call_count);
	g_recv_seq = block->seq;
	/* 框架自动释放；不要在这里调用 release */
	k_sem_give(&g_test_sem);
}

static void retain_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *user_data)
{
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	atomic_inc(&g_call_count);
	g_recv_seq = block->seq;

	/* 保留供异步处理 */
	g_retained_block = data_bus_block_retain(block);
	k_sem_give(&g_test_sem);
}

static atomic_t g_manual_release_count;

static void manual_release_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block,
				       void *user_data)
{
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	atomic_inc(&g_manual_release_count);
	g_recv_seq = block->seq;

	/* manual_release=true：消费者自行管理引用 */
	data_bus_block_release(block);
	k_sem_give(&g_test_sem);
}

/* ============================================================================
 * ISR 发布测试（定时器回调在 ISR 上下文）
 * ============================================================================ */

static struct k_timer g_isr_timer;
static data_bus_channel_t *g_isr_ch = NULL;

static void isr_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	const uint8_t data[] = {0x99, 0x88, 0x77};
	(void)data_bus_publish(g_isr_ch, data, sizeof(data));
}

/* ============================================================================
 * 设置 / 清理
 * ============================================================================ */

static void data_bus_test_setup(void)
{
	k_sem_init(&g_test_sem, 0, K_SEM_MAX_LIMIT);
	atomic_set(&g_call_count, 0);
	atomic_set(&g_manual_release_count, 0);
	g_recv_seq = 0;
	g_retained_block = NULL;
	g_isr_ch = NULL;
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/**
 * @brief 测试 Data Bus 初始化与反初始化
 */
ZTEST(test_data_bus, test_init_deinit)
{
	data_bus_test_setup();

	int ret = data_bus_init();
	zassert_equal(ret, 0, "data_bus_init 失败");

	/* 重复初始化应成功（幂等） */
	ret = data_bus_init();
	zassert_equal(ret, 0, "重复初始化应成功");

	ret = data_bus_deinit();
	zassert_equal(ret, 0, "data_bus_deinit 失败");
}

/**
 * @brief 测试通道创建、查找与销毁
 */
ZTEST(test_data_bus, test_channel_create_destroy)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;

	/* 创建通道 */
	int ret = data_bus_channel_create("test_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");
	zassert_not_null(ch, "通道不应为 NULL");

	/* 查找通道 */
	data_bus_channel_t *found = data_bus_channel_find("test_ch");
	zassert_equal(found, ch, "查找应返回同一通道");

	/* 尝试重复名称 */
	data_bus_channel_t *ch2 = NULL;
	ret = data_bus_channel_create("test_ch", &ch2);
	zassert_equal(ret, -EEXIST, "重复名称应返回 -EEXIST");

	/* 销毁 */
	ret = data_bus_channel_destroy(ch);
	zassert_equal(ret, 0, "通道销毁失败");

	/* 销毁后查找 */
	found = data_bus_channel_find("test_ch");
	zassert_is_null(found, "销毁后查找应返回 NULL");

	data_bus_deinit();
}

/**
 * @brief 测试自动释放的发布与消费
 */
ZTEST(test_data_bus, test_publish_auto_release)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("auto_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册 manual_release=false（启用自动释放）的消费者 */
	data_bus_consumer_cfg_t cfg = {
		.name = "auto_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 发布数据 */
	const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "发布失败");

	/* 等待消费者回调 */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "消费者回调超时");
	zassert_equal(atomic_get(&g_call_count), 1, "消费者应被调用一次");
	zassert_equal(g_recv_seq, 0, "第一个 seq 应为 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试多消费者自动释放
 */
ZTEST(test_data_bus, test_multi_consumer)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("multi_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册两个自动释放消费者 */
	data_bus_consumer_cfg_t cfg1 = {
		.name = "consumer_a",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg1, NULL);
	zassert_equal(ret, 0, "消费者 A 注册失败");

	data_bus_consumer_cfg_t cfg2 = {
		.name = "consumer_b",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg2, NULL);
	zassert_equal(ret, 0, "消费者 B 注册失败");

	/* 发布数据 */
	const uint8_t test_data[] = {0xAB, 0xCD};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "发布失败");

	/* 等待两个消费者（2 次回调） */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "第一个消费者超时");
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "第二个消费者超时");

	zassert_equal(atomic_get(&g_call_count), 2, "两个消费者都应被调用");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试 data_bus_block_retain 异步持有
 */
ZTEST(test_data_bus, test_retain)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("retain_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册 retain 消费者的块 */
	data_bus_consumer_cfg_t cfg = {
		.name = "retain_consumer",
		.manual_release = false,
		.callback = retain_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 发布数据 */
	const char *msg = "retain me";
	size_t msg_len = strlen(msg) + 1;
	ret = data_bus_publish(ch, msg, msg_len);
	zassert_equal(ret, 0, "发布失败");

	/* 等待回调 */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "消费者回调超时");
	zassert_equal(atomic_get(&g_call_count), 1, "消费者应被调用一次");

	/* 被 retain 的块应该仍然有效 */
	zassert_not_null(g_retained_block, "retain 的块不应为 NULL");
	zassert_equal(g_retained_block->len, msg_len, "retain 长度不匹配");
	zassert_mem_equal(g_retained_block->ptr, msg, msg_len,
			  "retain 数据不匹配");

	/* 释放被 retain 的块 */
	data_bus_block_release(g_retained_block);
	g_retained_block = NULL;

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试 manual_release=true 的消费者自行管理引用
 */
ZTEST(test_data_bus, test_manual_release)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("manual_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册 manual_release=true 的消费者 */
	data_bus_consumer_cfg_t cfg = {
		.name = "manual_consumer",
		.manual_release = true,
		.callback = manual_release_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 发布数据 */
	const uint8_t test_data[] = {0x01, 0x02};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "发布失败");

	/* 等待消费者回调 */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "消费者回调超时");
	zassert_equal(atomic_get(&g_manual_release_count), 1, "消费者应被调用一次");
	zassert_equal(g_recv_seq, 0, "seq 应为 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试队列溢出处理
 */
ZTEST(test_data_bus, test_queue_overflow)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("overflow_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 填满队列（无消费者，块留在队列中） */
	const uint8_t test_data[] = {0x01};
	int published = 0;

	for (int i = 0; i < CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH + 5; i++) {
		ret = data_bus_publish(ch, test_data, sizeof(test_data));
		if (ret == 0) {
			published++;
		} else {
			zassert_equal(ret, -ENOBUFS, "满时应返回 -ENOBUFS");
		}
	}

	zassert_equal(published, CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH,
		      "应恰好发布 queue_depth 个条目");

	/* 检查统计 */
	data_bus_stats_t stats;
	data_bus_channel_get_stats(ch, &stats);
	zassert_true(stats.drop_count > 0, "drop_count 应 > 0");
	zassert_true(stats.queue_full_count > 0, "queue_full_count 应 > 0");

	/* 队列非空时 destroy 应返回 -EAGAIN */
	ret = data_bus_channel_destroy(ch);
	zassert_equal(ret, -EAGAIN, "队列非空时应返回 -EAGAIN");

	data_bus_deinit();
}

/**
 * @brief 测试反初始化清理挂起块
 */
ZTEST(test_data_bus, test_deinit_cleanup)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("cleanup_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 发布一些数据（无消费者） */
	const uint8_t test_data[] = {0x01, 0x02, 0x03};
	for (int i = 0; i < 3; i++) {
		ret = data_bus_publish(ch, test_data, sizeof(test_data));
		zassert_equal(ret, 0, "发布失败");
	}

	/* 反初始化应清理一切且不泄漏 */
	ret = data_bus_deinit();
	zassert_equal(ret, 0, "反初始化失败");

	/* 验证通道已不存在 */
	data_bus_channel_t *found = data_bus_channel_find("cleanup_ch");
	zassert_is_null(found, "反初始化后通道应不存在");

	/* 重新初始化应干净工作 */
	ret = data_bus_init();
	zassert_equal(ret, 0, "反初始化后重新初始化失败");

	/* 创建新通道 */
	data_bus_channel_t *ch2 = NULL;
	ret = data_bus_channel_create("new_ch", &ch2);
	zassert_equal(ret, 0, "重新初始化后通道创建失败");

	data_bus_channel_destroy(ch2);
	data_bus_deinit();
}

/**
 * @brief 无消费者或 consumer_count==0 时分发线程仍须排空 ring 队列（防块泄漏）
 */
ZTEST(test_data_bus, test_dispatch_drains_without_consumer)
{
	data_bus_test_setup();
	zassert_equal(data_bus_init(), 0, NULL);

	data_bus_channel_t *ch = NULL;
	zassert_equal(data_bus_channel_create("orphan_ch", &ch), 0, NULL);

	const uint8_t p1[] = {0x01};
	zassert_equal(data_bus_publish(ch, p1, sizeof(p1)), 0, NULL);
	k_msleep(150);
	zassert_equal(data_bus_channel_destroy(ch), 0, "无消费者 publish 排空后应可销毁");

	/* 最后一个消费者注销后再次 publish，队列须在 consumer_count==0 时被排空 */
	ch = NULL;
	zassert_equal(data_bus_channel_create("orphan2", &ch), 0, NULL);
	data_bus_consumer_cfg_t cfg = {
		.name = "tmp_cons",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	data_bus_consumer_t *cons = NULL;
	zassert_equal(data_bus_consumer_register(ch, &cfg, &cons), 0, NULL);
	const uint8_t p2[] = {0x02};
	zassert_equal(data_bus_publish(ch, p2, sizeof(p2)), 0, NULL);
	zassert_equal(k_sem_take(&g_test_sem, K_MSEC(200)), 0, NULL);
	zassert_equal(data_bus_consumer_unregister(cons), 0, NULL);
	const uint8_t p3[] = {0x03};
	zassert_equal(data_bus_publish(ch, p3, sizeof(p3)), 0, NULL);
	k_msleep(150);
	zassert_equal(data_bus_channel_destroy(ch), 0, "注销全部消费者后 publish 仍须可销毁");

	zassert_equal(data_bus_deinit(), 0, NULL);
}

/**
 * @brief 测试消费者注销
 */
ZTEST(test_data_bus, test_consumer_unregister)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("unregister_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	data_bus_consumer_t *consumer = NULL;
	data_bus_consumer_cfg_t cfg = {
		.name = "temp_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, &consumer);
	zassert_equal(ret, 0, "消费者注册失败");
	zassert_not_null(consumer, "out_consumer 应被设置");

	/* 注销 */
	ret = data_bus_consumer_unregister(consumer);
	zassert_equal(ret, 0, "消费者注销失败");

	/* 注销后发布 — 不应触发回调 */
	const uint8_t test_data[] = {0x01};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "发布失败");

	/* 给分发线程足够时间：无消费者时仍应出队并释放块（不触发回调） */
	k_sleep(K_MSEC(100));
	zassert_equal(atomic_get(&g_call_count), 0, "注销后回调不应触发");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试 publish_block（零拷贝）
 */
ZTEST(test_data_bus, test_publish_block)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("block_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册自动释放消费者 */
	data_bus_consumer_cfg_t cfg = {
		.name = "block_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 分配块并填充数据 */
	const char *msg = "zero-copy message";
	size_t msg_len = strlen(msg) + 1;
	data_bus_block_t *block = data_bus_mem_alloc(msg_len);
	zassert_not_null(block, "mem_alloc 失败");

	memcpy(block->ptr, msg, msg_len);
	block->len = msg_len;

	/* 发布块 */
	ret = data_bus_publish_block(ch, block);
	zassert_equal(ret, 0, "publish_block 失败");

	/* 等待消费者回调 */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "消费者回调超时");
	zassert_equal(atomic_get(&g_call_count), 1, "消费者应被调用一次");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试从 ISR 上下文发布数据
 */
ZTEST(test_data_bus, test_publish_from_isr)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("isr_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册消费者 */
	data_bus_consumer_cfg_t cfg = {
		.name = "isr_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 设置 ISR 发布目标 */
	g_isr_ch = ch;
	k_timer_init(&g_isr_timer, isr_timer_handler, NULL);
	k_timer_start(&g_isr_timer, K_MSEC(10), K_NO_WAIT);

	/* 等待 ISR 发布的数据被消费 */
	ret = k_sem_take(&g_test_sem, K_MSEC(200));
	zassert_equal(ret, 0, "ISR 发布消费者超时");
	zassert_equal(atomic_get(&g_call_count), 1, "消费者应被调用一次");

	k_timer_stop(&g_isr_timer);
	g_isr_ch = NULL;

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief 测试统计获取与重置
 */
ZTEST(test_data_bus, test_reset_stats)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("stats_ch", &ch);
	zassert_equal(ret, 0, "通道创建失败");

	/* 注册消费者 */
	data_bus_consumer_cfg_t cfg = {
		.name = "stats_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "消费者注册失败");

	/* 发布数据 */
	const uint8_t test_data[] = {0x01, 0x02};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "发布失败");

	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "消费者超时");

	/* 验证统计非零 */
	data_bus_stats_t stats;
	data_bus_channel_get_stats(ch, &stats);
	zassert_equal(stats.publish_count, 1, "publish_count 应为 1");
	zassert_equal(stats.consumer_count, 1, "consumer_count 应为 1");

	/* 重置统计 */
	data_bus_reset_stats(ch);
	data_bus_channel_get_stats(ch, &stats);
	zassert_equal(stats.publish_count, 0, "publish_count 应为 0");
	zassert_equal(stats.drop_count, 0, "drop_count 应为 0");
	zassert_equal(stats.queue_full_count, 0, "queue_full_count 应为 0");
	zassert_equal(stats.alloc_fail_count, 0, "alloc_fail_count 应为 0");
	zassert_equal(stats.consumer_count, 1, "consumer_count 应保持 1");
	zassert_equal(stats.peak_queue_usage, 0, "peak_queue_usage 应为 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/* ============================================================================
 * 测试套件
 * ============================================================================ */

ZTEST_SUITE(test_data_bus, NULL, NULL, NULL, NULL, NULL);
