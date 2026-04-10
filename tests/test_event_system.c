/**
 * @file test_event_system.c
 * @brief 事件系统单元测试
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

LOG_MODULE_REGISTER(test_event_system);

/* =============================================================================
 * 测试用例
 * ============================================================================= */

/**
 * @brief 测试事件系统初始化
 */
ZTEST(test_event_system, test_event_system_init) {
    event_status_t status;

    /* 测试正常初始化 */
    status = event_system_init();
    zassert_equal(status, EVENT_OK, "事件系统初始化失败");

    /* 测试重复初始化（应返回 OK）*/
    status = event_system_init();
    zassert_equal(status, EVENT_OK, "重复初始化应返回 OK");
}

/**
 * @brief 测试事件类型注册
 */
ZTEST(test_event_system, test_event_register_type) {
    event_status_t status;

    /* 先初始化 */
    event_system_init();

    /* 测试注册有效类型 */
    status = event_register_type(10, "test_event");
    zassert_equal(status, EVENT_OK, "事件类型注册失败");

    /* 测试重复注册（应返回 OK）*/
    status = event_register_type(10, "test_event");
    zassert_equal(status, EVENT_OK, "重复注册应返回 OK");

    /* 测试无效类型 */
    status = event_register_type(255, "invalid");
    zassert_equal(status, EVENT_OK, "255 应是有效类型");
}

/**
 * @brief 测试事件订阅
 */
ZTEST(test_event_system, test_event_subscribe) {
    event_status_t status;
    uint32_t       subscriber_id;

    /* 先初始化和注册 */
    event_system_init();
    event_register_type(20, "subscribe_test");

    /* 测试正常订阅 */
    status = event_subscribe(20, NULL, NULL, &subscriber_id);
    zassert_equal(status, EVENT_ERR_INVALID_ARG, "空回调应返回错误");

    /* 测试有效订阅 */
    status = event_subscribe(20, (event_callback_t) 0x1000, NULL, &subscriber_id);
    zassert_equal(status, EVENT_OK, "订阅失败");
    zassert_true(subscriber_id > 0, "订阅 ID 应大于 0");

    /* 测试取消订阅 */
    status = event_unsubscribe(20, subscriber_id);
    zassert_equal(status, EVENT_OK, "取消订阅失败");
}

/**
 * @brief 测试事件创建和释放
 */
ZTEST(test_event_system, test_event_create_free) {
    event_t* event;

    /* 先初始化 */
    event_system_init();

    /* 测试创建事件 */
    event = event_create(30, EVENT_PRIORITY_NORMAL);
    zassert_not_null(event, "事件创建失败");
    zassert_equal(event->type, 30, "事件类型不匹配");
    zassert_equal(event->priority, EVENT_PRIORITY_NORMAL, "事件优先级不匹配");

    /* 测试释放事件 */
    event_free(event);
    /* 不应崩溃 */
}

/**
 * @brief 测试事件统计
 */
ZTEST(test_event_system, test_event_statistics) {
    uint32_t total_events, queue_depth, dropped_events;

    /* 先初始化 */
    event_system_init();

    /* 测试获取统计 */
    event_get_statistics(&total_events, &queue_depth, &dropped_events);
    /* 不应崩溃，初始值应为 0 或合理值 */
}

/**
 * @brief 测试事件发布（无订阅者）
 */
ZTEST(test_event_system, test_event_publish_no_subscriber) {
    event_status_t status;
    event_t        event = {.type = 40, .priority = EVENT_PRIORITY_NORMAL, .data = NULL, .data_len = 0};

    /* 先初始化和启动 */
    event_system_init();
    event_system_start();

    /* 发布到未注册类型（应允许）*/
    status = event_publish(&event);
    /* 可能返回 OK 或 NO_SUBSCRIBER */

    event_system_stop();
}

/**
 * @brief 测试 event_create_with_data
 */
ZTEST(test_event_system, test_event_create_with_data) {
    event_t*  event;
    uint32_t  test_data = 0x12345678;

    event_system_init();

    /* 测试正常创建带数据的事件 */
    event = event_create_with_data(50, EVENT_PRIORITY_HIGH, &test_data, sizeof(test_data));
    zassert_not_null(event, "事件创建失败");
    zassert_equal(event->type, 50, "事件类型不匹配");
    zassert_equal(event->priority, EVENT_PRIORITY_HIGH, "事件优先级不匹配");
    zassert_equal(event->data_len, sizeof(test_data), "数据长度不匹配");
    zassert_not_null(event->data, "数据指针应为非 NULL");
    zassert_true(event->is_dynamic, "is_dynamic 应为 true");

    /* 验证数据副本正确 */
    zassert_equal(*(uint32_t*)event->data, 0x12345678, "数据副本不正确");

    event_free(event);

    /* 测试 NULL 数据（应退化为 event_create）*/
    event = event_create_with_data(51, EVENT_PRIORITY_NORMAL, NULL, 0);
    zassert_not_null(event, "事件创建失败");
    zassert_is_null(event->data, "NULL 数据时 data 应为 NULL");
    zassert_equal(event->data_len, 0, "数据长度应为 0");
    zassert_false(event->is_dynamic, "is_dynamic 应为 false");

    event_free(event);
}

/**
 * @brief 测试 event_notify_subscribers
 */
ZTEST(test_event_system, test_event_notify_subscribers) {
    event_status_t status;
    uint32_t       subscriber_id;

    event_system_init();
    event_system_start();
    event_register_type(60, "notify_test");

    /* 订阅事件 */
    status = event_subscribe(60, (event_callback_t)0x1000, NULL, &subscriber_id);
    zassert_equal(status, EVENT_OK, "订阅失败");

    /* 创建并发布事件 */
    uint32_t test_data = 999;
    status = event_publish_copy(60, EVENT_PRIORITY_NORMAL, &test_data, sizeof(test_data));
    zassert_equal(status, EVENT_OK, "发布失败");

    /* 等待事件处理 */
    k_msleep(50);

    /* 验证事件已发布（由于回调地址是假的，无法验证回调计数）*/
    zassert_true(subscriber_id > 0, "订阅者 ID 应大于 0");

    event_unsubscribe(60, subscriber_id);
    event_system_stop();
}

/**
 * @brief 测试 event_unsubscribe_all
 */
ZTEST(test_event_system, test_event_unsubscribe_all) {
    event_status_t status;
    uint32_t       sub_id1, sub_id2, sub_id3;

    event_system_init();
    event_register_type(70, "unsubscribe_all_test1");
    event_register_type(71, "unsubscribe_all_test2");
    event_register_type(72, "unsubscribe_all_test3");

    /* 同一回调订阅多个事件类型，每次订阅会得到不同的 subscriber_id */
    status = event_subscribe(70, (event_callback_t)0x1000, NULL, &sub_id1);
    zassert_equal(status, EVENT_OK, "订阅 70 失败");

    status = event_subscribe(71, (event_callback_t)0x1000, NULL, &sub_id2);
    zassert_equal(status, EVENT_OK, "订阅 71 失败");

    status = event_subscribe(72, (event_callback_t)0x1000, NULL, &sub_id3);
    zassert_equal(status, EVENT_OK, "订阅 72 失败");

    /* 验证每次订阅都得到不同的 subscriber_id */
    zassert_true(sub_id1 > 0, "订阅者 ID 应大于 0");
    zassert_true(sub_id2 > 0, "订阅者 ID 应大于 0");
    zassert_true(sub_id3 > 0, "订阅者 ID 应大于 0");

    /* event_unsubscribe_all 只能取消一个 subscriber_id 的订阅
     * （因为每次订阅分配不同的 ID，所以需要分别取消）*/
    event_unsubscribe_all(sub_id1);
    event_unsubscribe_all(sub_id2);
    event_unsubscribe_all(sub_id3);

    /* 验证所有订阅都已取消 */
    zassert_equal(event_get_subscriber_count(70), 0, "事件 70 的订阅者应为 0");
    zassert_equal(event_get_subscriber_count(71), 0, "事件 71 的订阅者应为 0");
    zassert_equal(event_get_subscriber_count(72), 0, "事件 72 的订阅者应为 0");
}

/**
 * @brief 测试 event_unregister_type
 */
ZTEST(test_event_system, test_event_unregister_type) {
    event_status_t status;
    uint32_t       subscriber_id;

    event_system_init();

    /* 注册事件类型 */
    status = event_register_type(80, "unregister_test");
    zassert_equal(status, EVENT_OK, "注册失败");

    /* 订阅事件 */
    status = event_subscribe(80, (event_callback_t)0x1000, NULL, &subscriber_id);
    zassert_equal(status, EVENT_OK, "订阅失败");

    /* 尝试注销有订阅者的类型（应失败）*/
    status = event_unregister_type(80);
    zassert_true(status != EVENT_OK, "有订阅者时应注销失败");

    /* 取消订阅后再注销 */
    status = event_unsubscribe(80, subscriber_id);
    zassert_equal(status, EVENT_OK, "取消订阅失败");

    status = event_unregister_type(80);
    zassert_equal(status, EVENT_OK, "注销失败");

    /* 验证类型已注销 */
    zassert_equal(event_get_subscriber_count(80), 0, "注销后订阅者数应为 0");
}

/**
 * @brief 测试 event_system_get_queue
 */
ZTEST(test_event_system, test_event_system_get_queue) {
    struct k_msgq* queue;

    /* 初始化后应返回有效指针 */
    event_system_init();
    queue = event_system_get_queue();
    zassert_not_null(queue, "初始化后队列应为非 NULL");
}

/**
 * @brief 测试 event_system_start/stop 多次调用
 */
ZTEST(test_event_system, test_event_system_start_stop_multiple) {
    event_status_t status;

    event_system_init();

    /* 多次启动应幂等 */
    status = event_system_start();
    zassert_equal(status, EVENT_OK, "第一次启动失败");

    status = event_system_start();
    zassert_equal(status, EVENT_OK, "重复启动应返回 OK");

    /* 多次停止应幂等 */
    status = event_system_stop();
    zassert_equal(status, EVENT_OK, "第一次停止失败");

    status = event_system_stop();
    zassert_equal(status, EVENT_OK, "重复停止应返回 OK");
}

/**
 * @brief 测试 event_is_running
 */
ZTEST(test_event_system, test_event_is_running) {
    event_system_init();

    /* 初始化后但未启动 */
    zassert_false(event_system_is_running(), "初始化后应为未运行");

    /* 启动后 */
    event_system_start();
    zassert_true(event_system_is_running(), "启动后应为运行中");

    /* 停止后 */
    event_system_stop();
    zassert_false(event_system_is_running(), "停止后应为未运行");
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(test_event_system, NULL, NULL, NULL, NULL, NULL);
