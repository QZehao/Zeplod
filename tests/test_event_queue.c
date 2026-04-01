/**
 * @file test_event_queue.c
 * @brief 事件队列单元测试
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
#include "event_queue.h"

LOG_MODULE_REGISTER(test_event_queue);

/* =============================================================================
 * 测试用例
 * ============================================================================= */

/**
 * @brief 测试队列初始化
 */
ZTEST(test_event_queue, test_queue_init) {
    struct k_msgq  test_queue;
    char           buffer[10 * sizeof(event_t)];
    event_status_t status;

    /* 测试正常初始化 */
    status = event_queue_init(&test_queue, buffer, 10);
    zassert_equal(status, EVENT_OK, "队列初始化失败");

    /* 测试空参数 */
    status = event_queue_init(NULL, buffer, 10);
    zassert_equal(status, EVENT_ERR_INVALID_ARG, "应拒绝空队列参数");

    status = event_queue_init(&test_queue, NULL, 10);
    zassert_equal(status, EVENT_ERR_INVALID_ARG, "应拒绝空缓冲区参数");
}

/**
 * @brief 测试队列入队和出队
 */
ZTEST(test_event_queue, test_queue_enqueue_dequeue) {
    struct k_msgq  test_queue;
    char           buffer[10 * sizeof(event_t)];
    event_status_t status;
    event_t        event_in = {.type = 50, .priority = EVENT_PRIORITY_NORMAL, .data = NULL, .data_len = 0};
    event_t        event_out;

    /* 初始化 */
    event_queue_init(&test_queue, buffer, 10);

    /* 测试入队 */
    status = event_queue_enqueue(&test_queue, &event_in, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队失败");

    /* 测试队列深度 */
    zassert_equal(event_queue_depth(&test_queue), 1, "队列深度应为 1");

    /* 测试出队 */
    status = event_queue_dequeue(&test_queue, &event_out, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "出队失败");
    zassert_equal(event_out.type, 50, "事件类型不匹配");

    /* 测试空队列出队 */
    status = event_queue_dequeue(&test_queue, &event_out, K_NO_WAIT);
    zassert_equal(status, EVENT_ERR_QUEUE_EMPTY, "空队列应返回 EMPTY");
}

/**
 * @brief 测试队列满的情况
 */
ZTEST(test_event_queue, test_queue_full) {
    struct k_msgq  test_queue;
    char           buffer[3 * sizeof(event_t)];
    event_status_t status;
    event_t        event = {.type = 51, .priority = EVENT_PRIORITY_NORMAL};

    /* 初始化小队列 */
    event_queue_init(&test_queue, buffer, 3);

    /* 填满队列 */
    status = event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 1 失败");

    status = event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 2 失败");

    status = event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 3 失败");

    /* 队列已满，新入队应失败（DROP_NEWEST 策略）*/
    status = event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_ERR_QUEUE_FULL, "队列满时应返回 FULL");

    /* 测试 is_full */
    zassert_true(event_queue_is_full(&test_queue), "队列应标记为满");
}

/**
 * @brief 测试 DROP_LOWEST：丢弃队列中优先级最低的一条（同优先级 FIFO 最旧）
 */
ZTEST(test_event_queue, test_queue_overflow_drop_lowest) {
    struct k_msgq  test_queue;
    char           buffer[3 * sizeof(event_t)];
    event_status_t status;
    event_t        out;

    event_queue_init(&test_queue, buffer, 3);

    event_t e1 = {.type = 1, .priority = EVENT_PRIORITY_NORMAL};
    event_t e2 = {.type = 2, .priority = EVENT_PRIORITY_NORMAL};
    event_t e3 = {.type = 3, .priority = EVENT_PRIORITY_NORMAL};
    event_t hi = {.type = 99, .priority = EVENT_PRIORITY_HIGH};

    status = event_queue_enqueue(&test_queue, &e1, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 e1");
    status = event_queue_enqueue(&test_queue, &e2, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 e2");
    status = event_queue_enqueue(&test_queue, &e3, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "入队 e3");

    status = event_queue_enqueue(&test_queue, &hi, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "高优先级应挤掉队列中最低/最旧的一条");

    zassert_equal(event_queue_depth(&test_queue), 3, "深度仍为 3");

    status = event_queue_dequeue(&test_queue, &out, K_NO_WAIT);
    zassert_equal(status, EVENT_OK, "出队 1");
    zassert_equal(out.type, 2, "应丢弃最旧 NORMAL，保留 e2");

    status = event_queue_dequeue(&test_queue, &out, K_NO_WAIT);
    zassert_equal(out.type, 3, "e3");

    status = event_queue_dequeue(&test_queue, &out, K_NO_WAIT);
    zassert_equal(out.type, 99, "高优先级事件应在队尾入队");
}

/**
 * @brief 测试 DROP_LOWEST：新事件比队列中最差事件还低时丢弃新事件
 */
ZTEST(test_event_queue, test_queue_overflow_drop_lowest_reject_worse) {
    struct k_msgq  test_queue;
    char           buffer[3 * sizeof(event_t)];
    event_status_t status;
    event_t        out;

    event_queue_init(&test_queue, buffer, 3);

    for (int i = 0; i < 3; i++) {
        event_t hi = {.type = (uint32_t) (10 + i), .priority = EVENT_PRIORITY_HIGH};

        status = event_queue_enqueue(&test_queue, &hi, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
        zassert_equal(status, EVENT_OK, "填满 HIGH");
    }

    event_t lo = {.type = 20, .priority = EVENT_PRIORITY_LOW};

    status = event_queue_enqueue(&test_queue, &lo, QUEUE_OVERFLOW_DROP_LOWEST, K_NO_WAIT);
    zassert_equal(status, EVENT_ERR_QUEUE_FULL, "新事件更差时应拒绝入队");

    zassert_equal(event_queue_depth(&test_queue), 3, "队列内容不变");

    for (int t = 10; t <= 12; t++) {
        status = event_queue_dequeue(&test_queue, &out, K_NO_WAIT);
        zassert_equal(status, EVENT_OK, "出队");
        zassert_equal((int) out.type, t, "仍为原 HIGH 事件");
    }
}

/**
 * @brief 测试队列清空
 */
ZTEST(test_event_queue, test_queue_purge) {
    struct k_msgq test_queue;
    char          buffer[5 * sizeof(event_t)];
    event_t       event = {.type = 52, .priority = EVENT_PRIORITY_NORMAL};

    /* 初始化 */
    event_queue_init(&test_queue, buffer, 5);

    /* 添加一些事件 */
    for (int i = 0; i < 3; i++) {
        event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    }

    zassert_equal(event_queue_depth(&test_queue), 3, "队列深度应为 3");

    /* 清空队列 */
    event_queue_purge(&test_queue);

    zassert_equal(event_queue_depth(&test_queue), 0, "队列应为空");
    zassert_true(event_queue_is_empty(&test_queue), "队列应标记为空");
}

/**
 * @brief 测试队列统计
 */
ZTEST(test_event_queue, test_queue_stats) {
    struct k_msgq test_queue;
    char          buffer[10 * sizeof(event_t)];
    queue_stats_t stats;
    event_t       event = {.type = 53, .priority = EVENT_PRIORITY_NORMAL};

    /* 初始化 */
    event_queue_init(&test_queue, buffer, 10);

    /* 入队一些事件 */
    for (int i = 0; i < 5; i++) {
        event_queue_enqueue(&test_queue, &event, QUEUE_OVERFLOW_DROP_NEWEST, K_NO_WAIT);
    }

    /* 出队一些事件 */
    event_t out;
    for (int i = 0; i < 3; i++) {
        event_queue_dequeue(&test_queue, &out, K_NO_WAIT);
    }

    /* 获取统计 */
    event_queue_get_stats(&test_queue, &stats);

    zassert_true(stats.enqueue_count >= 5, "入队计数应至少为 5");
    zassert_true(stats.dequeue_count >= 3, "出队计数应至少为 3");

    /* 重置统计 */
    event_queue_reset_stats(&test_queue);
    event_queue_get_stats(&test_queue, &stats);
    zassert_equal(stats.enqueue_count, 0, "重置后入队计数应为 0");
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(test_event_queue, NULL, NULL, NULL, NULL, NULL);
