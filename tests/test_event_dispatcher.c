/**
 * @file test_event_dispatcher.c
 * @brief event_dispatcher 单元测试（勿与 event_system_start 同时跑两套队列消费者，避免抢同一队列）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include "event_system.h"
#include "event_dispatcher.h"

LOG_MODULE_REGISTER(test_event_dispatcher);

ZTEST(event_dispatcher, test_init_start_stop) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_STOPPED, NULL);

    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_STOPPED, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_stats_reset) {
    dispatcher_stats_t stats;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    event_dispatcher_reset_stats();
    event_dispatcher_get_stats(&stats);
    zassert_equal(stats.events_processed, 0ULL, NULL);
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_pause_resume) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_pause(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_PAUSED, NULL);

    zassert_equal(event_dispatcher_resume(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST_SUITE(event_dispatcher, NULL, NULL, NULL, NULL, NULL);
