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
    status = event_register_type(256, "invalid");
    zassert_equal(status, EVENT_ERR_INVALID_ARG, "应拒绝无效类型");
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

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(test_event_system, NULL, NULL, NULL, NULL, NULL);
