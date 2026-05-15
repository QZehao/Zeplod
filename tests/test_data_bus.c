/**
 * @file test_data_bus.c
 * @brief Data Bus unit tests
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "data_bus.h"

LOG_MODULE_REGISTER(test_data_bus);

/* ============================================================================
 * Test fixtures
 * ============================================================================ */

static struct k_sem g_test_sem;
static int g_call_count = 0;
static uint32_t g_recv_seq = 0;
static data_bus_block_t *g_retained_block = NULL;

/* ============================================================================
 * Consumer callbacks
 * ============================================================================ */

static void auto_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *user_data)
{
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	g_call_count++;
	g_recv_seq = block->seq;
	/* Framework auto-releases; do NOT call release here */
	k_sem_give(&g_test_sem);
}

static void retain_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *user_data)
{
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	g_call_count++;
	g_recv_seq = block->seq;

	/* Retain for async processing */
	g_retained_block = data_bus_block_retain(block);
	k_sem_give(&g_test_sem);
}

/* ============================================================================
 * Setup / teardown
 * ============================================================================ */

static void data_bus_test_setup(void)
{
	k_sem_init(&g_test_sem, 0, K_SEM_MAX_LIMIT);
	g_call_count = 0;
	g_recv_seq = 0;
	g_retained_block = NULL;
}

/* ============================================================================
 * Test cases
 * ============================================================================ */

/**
 * @brief Test data bus init and deinit
 */
ZTEST(test_data_bus, test_init_deinit)
{
	data_bus_test_setup();

	int ret = data_bus_init();
	zassert_equal(ret, 0, "data_bus_init failed");

	/* Re-init should succeed (idempotent) */
	ret = data_bus_init();
	zassert_equal(ret, 0, "re-init should succeed");

	ret = data_bus_deinit();
	zassert_equal(ret, 0, "data_bus_deinit failed");
}

/**
 * @brief Test channel create, find and destroy
 */
ZTEST(test_data_bus, test_channel_create_destroy)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;

	/* Create a channel */
	int ret = data_bus_channel_create("test_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");
	zassert_not_null(ch, "channel should not be NULL");

	/* Find the channel */
	data_bus_channel_t *found = data_bus_channel_find("test_ch");
	zassert_equal(found, ch, "find should return the same channel");

	/* Try duplicate name */
	data_bus_channel_t *ch2 = NULL;
	ret = data_bus_channel_create("test_ch", &ch2);
	zassert_equal(ret, -EEXIST, "duplicate name should return -EEXIST");

	/* Destroy */
	ret = data_bus_channel_destroy(ch);
	zassert_equal(ret, 0, "channel destroy failed");

	/* Find after destroy */
	found = data_bus_channel_find("test_ch");
	zassert_is_null(found, "find after destroy should return NULL");

	data_bus_deinit();
}

/**
 * @brief Test auto-release publish and consume
 */
ZTEST(test_data_bus, test_publish_auto_release)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("auto_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Register consumer with manual_release=false (auto-release enabled) */
	data_bus_consumer_cfg_t cfg = {
		.name = "auto_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "consumer register failed");

	/* Publish data */
	const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "publish failed");

	/* Wait for consumer callback */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "consumer callback timeout");
	zassert_equal(g_call_count, 1, "consumer should have been called once");
	zassert_equal(g_recv_seq, 0, "first seq should be 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief Test multiple consumers with auto-release
 */
ZTEST(test_data_bus, test_multi_consumer)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("multi_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Register two auto-release consumers */
	data_bus_consumer_cfg_t cfg1 = {
		.name = "consumer_a",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg1, NULL);
	zassert_equal(ret, 0, "consumer A register failed");

	data_bus_consumer_cfg_t cfg2 = {
		.name = "consumer_b",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg2, NULL);
	zassert_equal(ret, 0, "consumer B register failed");

	/* Publish data */
	const uint8_t test_data[] = {0xAB, 0xCD};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "publish failed");

	/* Wait for both consumers (2 callbacks) */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "first consumer timeout");
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "second consumer timeout");

	zassert_equal(g_call_count, 2, "both consumers should be called");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief Test data_bus_block_retain for async ownership
 */
ZTEST(test_data_bus, test_retain)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("retain_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Register consumer that retains the block */
	data_bus_consumer_cfg_t cfg = {
		.name = "retain_consumer",
		.manual_release = false,
		.callback = retain_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "consumer register failed");

	/* Publish data */
	const char *msg = "retain me";
	ret = data_bus_publish(ch, msg, strlen(msg) + 1);
	zassert_equal(ret, 0, "publish failed");

	/* Wait for callback */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "consumer callback timeout");
	zassert_equal(g_call_count, 1, "consumer should be called once");

	/* The retained block should still be valid */
	zassert_not_null(g_retained_block, "retained block should not be NULL");
	zassert_equal(g_retained_block->len, strlen(msg) + 1, "retained len mismatch");
	zassert_mem_equal(g_retained_block->ptr, msg, strlen(msg) + 1,
			  "retained data mismatch");

	/* Release the retained block */
	data_bus_block_release(g_retained_block);
	g_retained_block = NULL;

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief Test queue overflow handling
 */
ZTEST(test_data_bus, test_queue_overflow)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("overflow_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Fill the queue (no consumer, so blocks stay in queue) */
	const uint8_t test_data[] = {0x01};
	int published = 0;

	for (int i = 0; i < CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH + 5; i++) {
		ret = data_bus_publish(ch, test_data, sizeof(test_data));
		if (ret == 0) {
			published++;
		} else {
			zassert_equal(ret, -ENOBUFS, "should return -ENOBUFS when full");
		}
	}

	zassert_equal(published, CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH,
		      "should publish exactly queue_depth items");

	/* Check stats */
	data_bus_stats_t stats;
	data_bus_channel_get_stats(ch, &stats);
	zassert_true(stats.drop_count > 0, "drop_count should be > 0");
	zassert_true(stats.queue_full_count > 0, "queue_full_count should be > 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief Test deinit cleans up pending blocks
 */
ZTEST(test_data_bus, test_deinit_cleanup)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("cleanup_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Publish some data (no consumer) */
	const uint8_t test_data[] = {0x01, 0x02, 0x03};
	for (int i = 0; i < 3; i++) {
		ret = data_bus_publish(ch, test_data, sizeof(test_data));
		zassert_equal(ret, 0, "publish failed");
	}

	/* Deinit should clean up everything without leaking */
	ret = data_bus_deinit();
	zassert_equal(ret, 0, "deinit failed");

	/* Re-init should work cleanly */
	ret = data_bus_init();
	zassert_equal(ret, 0, "re-init after deinit failed");

	/* Create new channel */
	data_bus_channel_t *ch2 = NULL;
	ret = data_bus_channel_create("new_ch", &ch2);
	zassert_equal(ret, 0, "channel create after re-init failed");

	data_bus_channel_destroy(ch2);
	data_bus_deinit();
}

/**
 * @brief Test consumer unregister
 */
ZTEST(test_data_bus, test_consumer_unregister)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("unregister_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	data_bus_consumer_t *consumer = NULL;
	data_bus_consumer_cfg_t cfg = {
		.name = "temp_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, &consumer);
	zassert_equal(ret, 0, "consumer register failed");
	zassert_not_null(consumer, "out_consumer should be set");

	/* Unregister */
	ret = data_bus_consumer_unregister(consumer);
	zassert_equal(ret, 0, "consumer unregister failed");

	/* Publish after unregister - should not trigger callback */
	const uint8_t test_data[] = {0x01};
	ret = data_bus_publish(ch, test_data, sizeof(test_data));
	zassert_equal(ret, 0, "publish failed");

	/* Wait a bit - callback should NOT fire */
	ret = k_sem_take(&g_test_sem, K_MSEC(50));
	zassert_equal(ret, -EAGAIN, "callback should not fire after unregister");
	zassert_equal(g_call_count, 0, "call count should remain 0");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/**
 * @brief Test publish_block (zero-copy)
 */
ZTEST(test_data_bus, test_publish_block)
{
	data_bus_test_setup();
	data_bus_init();

	data_bus_channel_t *ch = NULL;
	int ret = data_bus_channel_create("block_ch", &ch);
	zassert_equal(ret, 0, "channel create failed");

	/* Register auto-release consumer */
	data_bus_consumer_cfg_t cfg = {
		.name = "block_consumer",
		.manual_release = false,
		.callback = auto_consumer_cb,
		.user_data = NULL,
	};
	ret = data_bus_consumer_register(ch, &cfg, NULL);
	zassert_equal(ret, 0, "consumer register failed");

	/* Allocate block and fill data */
	const char *msg = "zero-copy message";
	data_bus_block_t *block = data_bus_mem_alloc(strlen(msg) + 1);
	zassert_not_null(block, "mem_alloc failed");

	memcpy(block->ptr, msg, strlen(msg) + 1);
	block->len = strlen(msg) + 1;

	/* Publish block */
	ret = data_bus_publish_block(ch, block);
	zassert_equal(ret, 0, "publish_block failed");

	/* Wait for consumer callback */
	ret = k_sem_take(&g_test_sem, K_MSEC(100));
	zassert_equal(ret, 0, "consumer callback timeout");
	zassert_equal(g_call_count, 1, "consumer should be called once");

	data_bus_channel_destroy(ch);
	data_bus_deinit();
}

/* ============================================================================
 * Test suite
 * ============================================================================ */

ZTEST_SUITE(test_data_bus, NULL, NULL, NULL, NULL, NULL);
