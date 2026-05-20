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
#include "event_dispatcher.h"
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
    event_t        event = {0};

    event.type = 40;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.data_len = 0;

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

    /* 验证数据副本正确 - 小数据使用内联存储 */
    zassert_true((event->flags & EVENT_FLAG_DATA_INLINE) != 0 || (event->flags & EVENT_FLAG_DATA_DYNAMIC) != 0,
                 "数据应标记为内联或动态");

    if (event->flags & EVENT_FLAG_DATA_INLINE) {
        zassert_mem_equal(event->data.inline_data, &test_data, sizeof(test_data), "内联数据不正确");
    } else if (event->flags & EVENT_FLAG_DATA_DYNAMIC) {
        zassert_not_null(event->data.ptr, "动态数据指针不应为 NULL");
        zassert_equal(*(uint32_t*)event->data.ptr, 0x12345678, "动态数据副本不正确");
    }

    event_free(event);

    /* 测试 NULL 数据（应退化为 event_create）*/
    event = event_create_with_data(51, EVENT_PRIORITY_NORMAL, NULL, 0);
    zassert_not_null(event, "事件创建失败");
    zassert_equal(event->data_len, 0, "数据长度应为 0");
    zassert_false((event->flags & EVENT_FLAG_DATA_DYNAMIC) != 0, "NULL 数据时不应标记为动态");

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
 * @brief 测试 event_system_shutdown
 */
ZTEST(test_event_system, test_event_system_shutdown) {
    event_status_t status;
    uint32_t       subscriber_id;

    event_system_init();
    event_register_type(90, "shutdown_test");
    event_subscribe(90, (event_callback_t)0x1000, NULL, &subscriber_id);
    event_system_start();

    /* 关闭系统 */
    status = event_system_shutdown();
    zassert_equal(status, EVENT_OK, "关闭失败");

    /* 关闭后应为未初始化状态 */
    zassert_false(event_system_is_running(), "关闭后不应运行");

    /* 重新初始化应成功 */
    status = event_system_init();
    zassert_equal(status, EVENT_OK, "重新初始化失败");
}

/**
 * @brief 测试 event_get_type_name
 */
ZTEST(test_event_system, test_event_get_type_name) {
    const char* name;

    event_system_init();

    /* 注册类型 */
    event_register_type(100, "test_type_name");

    /* 获取已注册类型名称 */
    name = event_get_type_name(100);
    zassert_str_equal(name, "test_type_name", "类型名称不匹配");

    /* 获取未注册类型名称 */
    name = event_get_type_name(101);
    zassert_str_equal(name, "UNREGISTERED", "未注册类型应返回 UNREGISTERED");

    /* 获取无效类型 ID */
    name = event_get_type_name(255);
    zassert_str_equal(name, "UNREGISTERED", "无效类型应返回 UNREGISTERED");
}

/**
 * @brief 测试订阅者上限
 */
ZTEST(test_event_system, test_event_subscribe_max_subscribers) {
    event_status_t status;
    uint32_t       subscriber_ids[CONFIG_EVENT_MAX_SUBSCRIBERS + 1];

    event_system_init();
    event_register_type(110, "max_sub_test");

    /* 订阅直到达到上限 */
    for (int i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        status = event_subscribe(110, (event_callback_t)(0x1000 + i), NULL, &subscriber_ids[i]);
        zassert_equal(status, EVENT_OK, "订阅 %d 失败", i);
    }

    /* 超过上限应失败 */
    status = event_subscribe(110, (event_callback_t)0x2000, NULL, &subscriber_ids[CONFIG_EVENT_MAX_SUBSCRIBERS]);
    zassert_equal(status, EVENT_ERR_QUEUE_FULL, "超过订阅上限应返回 QUEUE_FULL");

    /* 清理 */
    for (int i = 0; i < CONFIG_EVENT_MAX_SUBSCRIBERS; i++) {
        event_unsubscribe(110, subscriber_ids[i]);
    }
}

/**
 * @brief 测试 event_create_rt 实时安全创建
 */
ZTEST(test_event_system, test_event_create_rt) {
    event_t* event;

    event_system_init();

    /* 测试创建 CRITICAL 优先级事件 */
    event = event_create_rt(120, EVENT_PRIORITY_CRITICAL);
    if (event != NULL) {
        zassert_equal(event->type, 120, "事件类型不匹配");
        zassert_equal(event->priority, EVENT_PRIORITY_CRITICAL, "优先级不匹配");
        zassert_true((event->flags & EVENT_FLAG_FROM_SLAB) != 0, "应标记为来自 Slab");
        event_free(event);
    }

    /* 测试创建 HIGH 优先级事件 */
    event = event_create_rt(121, EVENT_PRIORITY_HIGH);
    if (event != NULL) {
        zassert_equal(event->type, 121, "事件类型不匹配");
        event_free(event);
    }

    /* 测试创建 NORMAL 优先级事件 */
    event = event_create_rt(122, EVENT_PRIORITY_NORMAL);
    if (event != NULL) {
        zassert_equal(event->type, 122, "事件类型不匹配");
        event_free(event);
    }

    /* 测试创建 LOW 优先级事件 */
    event = event_create_rt(123, EVENT_PRIORITY_LOW);
    if (event != NULL) {
        zassert_equal(event->type, 123, "事件类型不匹配");
        event_free(event);
    }
}

/**
 * @brief 测试 event_create_with_data_rt 实时安全带数据创建
 */
ZTEST(test_event_system, test_event_create_with_data_rt) {
    event_t* event;
    uint8_t  small_data[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t  large_data[256];

    for (int i = 0; i < 256; i++) {
        large_data[i] = (uint8_t)i;
    }

    event_system_init();

    /* 测试 NULL 数据（应退化为 event_create_rt）*/
    event = event_create_with_data_rt(130, EVENT_PRIORITY_NORMAL, NULL, 0);
    if (event != NULL) {
        zassert_equal(event->data_len, 0, "数据长度应为 0");
        event_free(event);
    }

    /* 测试小数据（内联存储）*/
    event = event_create_with_data_rt(131, EVENT_PRIORITY_NORMAL, small_data, sizeof(small_data));
    if (event != NULL) {
        zassert_equal(event->data_len, sizeof(small_data), "数据长度不匹配");
        zassert_true((event->flags & EVENT_FLAG_DATA_INLINE) != 0, "小数据应内联存储");
        zassert_mem_equal(event->data.inline_data, small_data, sizeof(small_data), "数据内容不匹配");
        event_free(event);
    }

    /* 测试大数据（需要 Slab）*/
    event = event_create_with_data_rt(132, EVENT_PRIORITY_NORMAL, large_data, sizeof(large_data));
    if (event != NULL) {
        zassert_equal(event->data_len, sizeof(large_data), "大数据长度不匹配");
        /* 注意：大数据可能使用 slab 或内联，取决于配置 */
        event_free(event);
    }
}

/**
 * @brief 测试 event_publish_copy_rt 实时安全发布
 */
ZTEST(test_event_system, test_event_publish_copy_rt) {
    event_status_t status;
    uint32_t       test_data = 0xDEADBEEF;

    event_system_init();
    event_system_start();
    event_register_type(140, "publish_rt_test");

    /* 测试发布 */
    status = event_publish_copy_rt(140, EVENT_PRIORITY_NORMAL, &test_data, sizeof(test_data));
    /* 可能成功或失败，取决于 Slab 配置 */
    if (status == EVENT_OK) {
        /* 等待事件处理 */
        k_msleep(50);
    }

    event_system_stop();
}

/**
 * @brief 测试发布到已停止系统
 */
ZTEST(test_event_system, test_event_publish_when_stopped) {
    event_status_t status;
    event_t        event = {.type = 150, .priority = EVENT_PRIORITY_NORMAL, .data_len = 0};

    event_system_init();
    /* 不启动系统 */

    /* 发布应被拒绝（未 start 时返回 NOT_RUNNING） */
    status = event_publish(&event);
    zassert_equal(status, EVENT_ERR_NOT_RUNNING, "停止状态下发布应被拒绝");

    /* event_publish_copy 也应被拒绝 */
    status = event_publish_copy(150, EVENT_PRIORITY_NORMAL, "test", 4);
    zassert_equal(status, EVENT_ERR_NOT_RUNNING, "停止状态下 publish_copy 应被拒绝");
}

/**
 * @brief 测试事件系统多次 shutdown
 */
ZTEST(test_event_system, test_event_system_shutdown_multiple) {
    event_status_t status;

    event_system_init();

    /* 第一次关闭 */
    status = event_system_shutdown();
    zassert_equal(status, EVENT_OK, "第一次关闭失败");

    /* 第二次关闭应幂等 */
    status = event_system_shutdown();
    zassert_equal(status, EVENT_OK, "重复关闭应返回 OK");
}

/**
 * @brief 测试订阅未注册类型
 */
ZTEST(test_event_system, test_event_subscribe_unregistered_type) {
    event_status_t status;
    uint32_t       subscriber_id;

    event_system_init();
    /* 不注册类型 160 */

    status = event_subscribe(160, (event_callback_t)0x1000, NULL, &subscriber_id);
    zassert_equal(status, EVENT_ERR_NOT_FOUND, "订阅未注册类型应返回 NOT_FOUND");
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
 * @brief 测试 event_system_stop 会停止分发器，start 时自动恢复
 */
ZTEST(test_event_system, test_event_system_stop_restarts_dispatcher) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_system_stop(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_STOPPED, NULL);

    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_get_state(), DISPATCHER_RUNNING, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_stop(), EVENT_OK, NULL);
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
 * 测试夹具 (Test Fixtures)
 * ============================================================================= */

static void *event_system_suite_setup(void)
{
    /* 全局初始化 - 允许重复初始化（返回 -EALREADY）*/
    event_system_init();
    event_system_start();
    return NULL;
}

static void event_system_suite_teardown(void *fixture)
{
    (void)fixture;
    event_system_stop();
}

/**
 * @brief 每个测试用例后的清理函数
 *
 * 确保测试间的状态隔离。
 */
static void event_system_test_teardown(void *fixture)
{
    (void)fixture;
    /* 停止并关闭事件系统，清理所有注册的类型和订阅 */
    event_system_stop();
    event_system_shutdown();
    /* 不重新初始化，让下一个测试用例自己调用 init/start */
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(test_event_system, NULL, event_system_suite_setup, event_system_suite_teardown, NULL, event_system_test_teardown);
