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

ZTEST(event_dispatcher, test_set_filter) {
    static int filter_call_count = 0;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);

    /* 定义过滤函数：只允许特定类型的事件 */
    bool test_filter(const event_t* event, void* user_data) {
        (void)user_data;
        filter_call_count++;
        return event->type == 100; /* 只允许 type=100 的事件 */
    }

    event_dispatcher_set_filter(test_filter, NULL);

    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布不同类型的事件 */
    event_publish_copy(100, EVENT_PRIORITY_NORMAL, "allowed", 7);
    event_publish_copy(101, EVENT_PRIORITY_NORMAL, "blocked", 7);

    k_msleep(100);

    zassert_true(filter_call_count > 0, "过滤器应被调用");

    event_dispatcher_clear_filter();
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_process_one) {
    event_status_t status;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布一个事件 */
    event_publish_copy(200, EVENT_PRIORITY_NORMAL, "test", 4);

    /* 等待事件进入队列 */
    k_msleep(20);

    /* 手动处理一个事件 */
    status = event_dispatcher_process_one(K_MSEC(100));
    zassert_true(status == EVENT_OK || status == EVENT_ERR_QUEUE_EMPTY, "process_one 应返回 OK 或 EMPTY");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_process_all) {
    uint32_t processed;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布多个事件 */
    for (int i = 0; i < 5; i++) {
        event_publish_copy(201 + i, EVENT_PRIORITY_NORMAL, "test", 4);
    }

    k_msleep(50);

    /* 处理所有事件 */
    processed = event_dispatcher_process_all(0); /* 0 表示使用默认上限 */
    zassert_true(processed <= 5, "处理数量不应超过发布数量");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_get_current_latency) {
    uint32_t latency;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 获取当前延迟（不应崩溃）*/
    latency = event_dispatcher_get_current_latency();
    /* 延迟值应该是一个合理的数值 */
    zassert_true(latency < 1000000, "延迟应小于 1 秒");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_stats_comprehensive) {
    dispatcher_stats_t stats;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布并处理一些事件 */
    for (int i = 0; i < 10; i++) {
        event_publish_copy(300, EVENT_PRIORITY_NORMAL, "stats", 5);
    }

    k_msleep(100);

    /* 获取统计 */
    event_dispatcher_get_stats(&stats);
    zassert_true(stats.events_processed <= 10, "处理事件数不应超过发布数");

    /* 重置统计 */
    event_dispatcher_reset_stats();
    event_dispatcher_get_stats(&stats);
    zassert_equal(stats.events_processed, 0ULL, "重置后处理计数应为 0");
    zassert_equal(stats.events_dropped, 0ULL, "重置后丢弃计数应为 0");
    zassert_equal(stats.processing_errors, 0ULL, "重置后错误计数应为 0");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST(event_dispatcher, test_custom_config) {
    dispatcher_config_t config = {
        .stack_size = 1024,
        .priority = 7,
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 50
    };

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);

    /* 使用自定义配置初始化 */
    zassert_equal(event_dispatcher_init(&config), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

ZTEST_SUITE(event_dispatcher, NULL, NULL, NULL, NULL, NULL);
