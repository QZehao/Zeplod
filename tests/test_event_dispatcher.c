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
    static int filter_call_count;

    filter_call_count = 0;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_register_type(100, "filter_t100"), EVENT_OK, NULL);
    zassert_equal(event_register_type(101, "filter_t101"), EVENT_OK, NULL);
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
        event_publish_copy(50, EVENT_PRIORITY_NORMAL, "stats", 5);
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

/**
 * @brief 测试无效配置参数 - 栈大小过小
 */
ZTEST(event_dispatcher, test_invalid_config_stack_too_small) {
    dispatcher_config_t config = {
        .stack_size = 128, /* 小于 EVENT_DISPATCHER_MIN_STACK_SIZE (256) */
        .priority = 5,
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 100
    };

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_init(&config), EVENT_ERR_INVALID_ARG, "栈过小应返回错误");

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试无效配置参数 - 栈大小过大
 */
ZTEST(event_dispatcher, test_invalid_config_stack_too_large) {
    dispatcher_config_t config = {
        .stack_size = 128 * 1024, /* 超过 EVENT_DISPATCHER_MAX_STACK_SIZE (64KB) */
        .priority = 5,
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 100
    };

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_init(&config), EVENT_ERR_INVALID_ARG, "栈过大应返回错误");

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试无效配置参数 - 优先级超出范围
 */
ZTEST(event_dispatcher, test_invalid_config_priority_out_of_range) {
    dispatcher_config_t config_low = {
        .stack_size = 1024,
        .priority = -1, /* 小于 EVENT_DISPATCHER_MIN_PRIORITY (0) */
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 100
    };

    dispatcher_config_t config_high = {
        .stack_size = 1024,
        .priority = 20, /* 大于 EVENT_DISPATCHER_MAX_PRIORITY (15) */
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 100
    };

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_init(&config_low), EVENT_ERR_INVALID_ARG, "优先级过低应返回错误");
    zassert_equal(event_dispatcher_init(&config_high), EVENT_ERR_INVALID_ARG, "优先级过高应返回错误");

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试无效配置参数 - max_events_per_cycle 过大
 */
ZTEST(event_dispatcher, test_invalid_config_max_events_too_large) {
    dispatcher_config_t config = {
        .stack_size = 1024,
        .priority = 5,
        .thread_name = "test_disp",
        .enable_stats = true,
        .max_events_per_cycle = 20000 /* 超过 EVENT_DISPATCHER_MAX_EVENTS_PER_CYCLE (10000) */
    };

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_init(&config), EVENT_ERR_INVALID_ARG, "max_events 过大应返回错误");

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试在 event_system_init 之前调用 dispatcher_init
 */
ZTEST(event_dispatcher, test_dispatcher_init_before_system_init) {
    /* 确保系统未初始化 */
    event_system_shutdown();

    zassert_equal(event_dispatcher_init(NULL), EVENT_ERR_INVALID_ARG, "未初始化系统时应返回错误");
}

/**
 * @brief 测试重复暂停
 */
ZTEST(event_dispatcher, test_double_pause) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 第一次暂停 */
    zassert_equal(event_dispatcher_pause(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_PAUSED, NULL);

    /* 第二次暂停应失败 */
    zassert_equal(event_dispatcher_pause(), EVENT_ERR_INVALID_ARG, "重复暂停应返回错误");

    zassert_equal(event_dispatcher_resume(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试重复恢复
 */
ZTEST(event_dispatcher, test_double_resume) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 未暂停时恢复应失败 */
    zassert_equal(event_dispatcher_resume(), EVENT_ERR_INVALID_ARG, "未暂停时恢复应返回错误");

    /* 暂停后恢复 */
    zassert_equal(event_dispatcher_pause(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_resume(), EVENT_OK, NULL);

    /* 再次恢复应失败 */
    zassert_equal(event_dispatcher_resume(), EVENT_ERR_INVALID_ARG, "重复恢复应返回错误");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试暂停状态下 process_one
 */
ZTEST(event_dispatcher, test_process_one_when_paused) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 暂停分发器 */
    zassert_equal(event_dispatcher_pause(), EVENT_OK, NULL);

    /* 暂停状态下 process_one 应返回错误 */
    zassert_equal(event_dispatcher_process_one(K_NO_WAIT), EVENT_ERR_INVALID_ARG, "暂停时 process_one 应返回错误");

    zassert_equal(event_dispatcher_resume(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试停止状态下 process_one
 */
ZTEST(event_dispatcher, test_process_one_when_stopped) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    /* 不启动分发器 */

    /* 停止状态下 process_one 应返回错误 */
    zassert_equal(event_dispatcher_process_one(K_NO_WAIT), EVENT_ERR_INVALID_ARG, "停止时 process_one 应返回错误");

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试过滤器阻止所有事件
 */
ZTEST(event_dispatcher, test_filter_block_all) {
    static uint32_t dropped_count;

    dropped_count = 0;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_register_type(210, "filter_block_t210"), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);

    /* 设置一个总是返回 false 的过滤器 */
    bool block_all_filter(const event_t* event, void* user_data) {
        (void)event;
        (void)user_data;
        dropped_count++;
        return false;
    }

    event_dispatcher_set_filter(block_all_filter, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布事件 */
    for (int i = 0; i < 5; i++) {
        event_publish_copy(210, EVENT_PRIORITY_NORMAL, "test", 4);
    }

    k_msleep(100);

    /* 验证事件被过滤 */
    zassert_true(dropped_count >= 5, "事件应被过滤器阻止");

    event_dispatcher_clear_filter();
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试多次启动
 */
ZTEST(event_dispatcher, test_multiple_start) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    /* 重复启动应幂等 */
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试多次停止
 */
ZTEST(event_dispatcher, test_multiple_stop) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_STOPPED, NULL);

    /* 重复停止应幂等 */
    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_STOPPED, NULL);

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

/**
 * @brief 测试 process_all 带上限
 */
ZTEST(event_dispatcher, test_process_all_with_limit) {
    uint32_t processed;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    /* 发布多个事件 */
    for (int i = 0; i < 10; i++) {
        event_publish_copy(220 + i, EVENT_PRIORITY_NORMAL, "test", 4);
    }

    k_msleep(50);

    /* 处理最多 3 个事件 */
    processed = event_dispatcher_process_all(3);
    zassert_true(processed <= 3, "处理数量不应超过上限");

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
}

static void event_dispatcher_after_each(void *fixture)
{
    (void)fixture;
    /* 完整 shutdown：清空已注册事件类型与分发器状态。仅 stop 不会清理类型表，
     * 否则本套件注册的 type（如 100/101/210）会污染后续 test_event_system 等套件。 */
    (void)event_system_shutdown();
}

ZTEST_SUITE(event_dispatcher, NULL, NULL, NULL, event_dispatcher_after_each, NULL);
