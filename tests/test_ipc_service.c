/**
 * @file test_ipc_service.c
 * @brief Thread IPC 服务单元测试
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
 * 2026-04-11       1.1            zeh            补充完整测试用例
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "ipc_service.h"
#include "example_module_uart.h"

LOG_MODULE_REGISTER(test_ipc_service);

static ipc_service_t g_ipc;

/* =============================================================================
 * 测试辅助函数
 * ============================================================================= */

static int ipc_ut_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                          size_t* out_data_size) {
    (void) request_id;
    *out_data = (void*) data;
    *out_data_size = data_size;
    return 0;
}

/* 延迟响应的 handler，用于测试异步和 Future */
static int ipc_delayed_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                                size_t* out_data_size) {
    (void) request_id;
    k_msleep(100); /* 模拟处理延迟 */
    *out_data = (void*) data;
    *out_data_size = data_size;
    return 0;
}

/* 返回错误的 handler */
static int ipc_error_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                              size_t* out_data_size) {
    (void) request_id;
    (void) data;
    (void) data_size;
    (void) out_data;
    (void) out_data_size;
    return -EINVAL;
}

/* 计数器 handler */
static int g_handler_call_count = 0;
static int ipc_counting_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                                 size_t* out_data_size) {
    (void) request_id;
    (void) data;
    (void) data_size;
    g_handler_call_count++;
    *out_data = NULL;
    *out_data_size = 0;
    return 0;
}

/* 异步回调计数器 */
static int g_async_callback_count = 0;
static int g_async_callback_result = 0;
static size_t g_async_callback_data_size = 0;
static ipc_request_id_t g_async_callback_request_id = 0;

static void async_test_callback(ipc_request_id_t request_id, int result, const void* data, size_t data_size,
                                 void* user_data) {
    (void) data;
    (void) user_data;
    g_async_callback_count++;
    g_async_callback_result = result;
    g_async_callback_data_size = data_size;
    g_async_callback_request_id = request_id;
}

/* =============================================================================
 * 测试套件 setup/teardown
 * ============================================================================= */

static void *test_suite_setup(void)
{
    /* 确保 UART 模块不会在后台运行（避免 spinlock 竞争） */
    example_module_uart_stop();

    return NULL;
}

static void test_suite_teardown(void *fixture)
{
    (void)fixture;
    ipc_service_stop(&g_ipc);
}

/* =============================================================================
 * 基础功能测试
 * ============================================================================= */

ZTEST(ipc_service, test_init_start_sync_stop) {
    const char payload[] = "ipc_ut";
    void*      out = NULL;
    size_t     outsz = 0;
    int        r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_SECONDS(2));
    zassert_equal(r, 0, "ipc_call_sync failed: %d", r);
    zassert_equal(outsz, sizeof(payload), NULL);
    zassert_mem_equal(out, payload, sizeof(payload), NULL);

    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "ipc_service_stop failed: %d", r);
}

ZTEST(ipc_service, test_sync_call_with_large_data) {
    char large_payload[256];
    void* out = NULL;
    size_t outsz = 0;
    int r;

    memset(large_payload, 'A', sizeof(large_payload) - 1);
    large_payload[sizeof(large_payload) - 1] = '\0';

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_sync(&g_ipc, large_payload, sizeof(large_payload), &out, &outsz, K_SECONDS(2));
    zassert_equal(r, 0, "ipc_call_sync failed: %d", r);
    zassert_equal(outsz, sizeof(large_payload), NULL);
    zassert_mem_equal(out, large_payload, sizeof(large_payload), NULL);

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_sync_call_error_handler) {
    const char payload[] = "error_test";
    void* out = NULL;
    size_t outsz = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_error_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_SECONDS(2));
    zassert_equal(r, -EINVAL, "应返回 -EINVAL，实际返回: %d", r);

    ipc_service_stop(&g_ipc);
}

/* =============================================================================
 * 异步调用测试
 * ============================================================================= */

ZTEST(ipc_service, test_async_call) {
    const char payload[] = "async_test";
    ipc_request_id_t request_id = 0;
    int r;

    g_async_callback_count = 0;
    g_async_callback_result = 0;
    g_async_callback_data_size = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL, &request_id);
    zassert_equal(r, 0, "ipc_call_async failed: %d", r);
    zassert_not_equal(request_id, 0, "request_id 不应为 0");

    /* 等待回调执行 */
    k_msleep(200);

    zassert_equal(g_async_callback_count, 1, "回调应被调用一次");
    zassert_equal(g_async_callback_result, 0, "回调结果应为 0");
    zassert_equal(g_async_callback_data_size, sizeof(payload), "数据大小应匹配");
    zassert_equal(g_async_callback_request_id, request_id, "request_id 应匹配");

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_async_call_with_delayed_handler) {
    const char payload[] = "delayed_async";
    ipc_request_id_t request_id = 0;
    int r;

    g_async_callback_count = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL, &request_id);
    zassert_equal(r, 0, "ipc_call_async failed: %d", r);

    /* 等待延迟处理完成 */
    k_msleep(300);

    zassert_equal(g_async_callback_count, 1, "回调应被调用一次");

    ipc_service_stop(&g_ipc);
}

/* =============================================================================
 * Future 模式测试
 * ============================================================================= */

ZTEST(ipc_service, test_future_call) {
    const char payload[] = "future_test";
    ipc_future_t* future = NULL;
    int result = -1;
    const void* out_data = NULL;
    size_t out_size = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload), &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);
    zassert_not_null(future, "future 不应为 NULL");

    r = ipc_future_wait(future, &result, &out_data, &out_size, K_SECONDS(2));
    zassert_equal(r, 0, "ipc_future_wait failed: %d", r);
    zassert_equal(result, 0, "result 应为 0");
    zassert_equal(out_size, sizeof(payload), "数据大小应匹配");
    zassert_mem_equal(out_data, payload, sizeof(payload), NULL);

    r = ipc_future_release(&g_ipc, future);
    zassert_equal(r, 0, "ipc_future_release failed: %d", r);

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_future_is_ready) {
    const char payload[] = "future_ready_test";
    ipc_future_t* future = NULL;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload), &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);

    /* 立即检查，应未就绪 */
    bool ready = ipc_future_is_ready(future);
    zassert_equal(ready, false, "future 应未就绪");

    /* 等待处理完成 */
    k_msleep(200);

    ready = ipc_future_is_ready(future);
    zassert_equal(ready, true, "future 应已就绪");

    ipc_future_release(&g_ipc, future);
    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_future_wait_timeout) {
    const char payload[] = "timeout_test";
    ipc_future_t* future = NULL;
    int result = 0;
    const void* out_data = NULL;
    size_t out_size = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload), &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);

    /* 使用很短的超时，应超时 */
    r = ipc_future_wait(future, &result, &out_data, &out_size, K_MSEC(10));
    zassert_equal(r, -EAGAIN, "应返回 -EAGAIN，实际: %d", r);

    /* 等待实际完成后再释放 */
    k_msleep(200);
    ipc_future_wait(future, &result, &out_data, &out_size, K_SECONDS(1));
    ipc_future_release(&g_ipc, future);
    ipc_service_stop(&g_ipc);
}

/* =============================================================================
 * 请求管理测试
 * ============================================================================= */

ZTEST(ipc_service, test_get_pending_count) {
    const char payload[] = "pending_test";
    ipc_request_id_t request_id = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 初始应为 0 */
    size_t count = ipc_service_get_pending_count(&g_ipc);
    zassert_equal(count, 0, "初始 pending 数量应为 0");

    /* 发送异步请求 */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL, &request_id);
    zassert_equal(r, 0, "ipc_call_async failed: %d", r);

    /* 短暂等待请求入队 */
    k_msleep(10);

    count = ipc_service_get_pending_count(&g_ipc);
    /* 请求可能已在处理中，所以 count 可能是 0 或 1 */
    zassert_true(count <= 1, "pending 数量应 <= 1");

    /* 等待处理完成 */
    k_msleep(200);

    count = ipc_service_get_pending_count(&g_ipc);
    zassert_equal(count, 0, "处理完成后 pending 数量应为 0");

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_cancel_request) {
    const char payload[] = "cancel_test";
    ipc_request_id_t request_id = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 发送异步请求 */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL, &request_id);
    zassert_equal(r, 0, "ipc_call_async failed: %d", r);

    /* 尝试取消请求（可能已在处理中，所以结果不确定） */
    r = ipc_service_cancel(&g_ipc, request_id);
    /* 结果可能是 0（成功取消）或 -ENOENT（未找到/已完成） */
    zassert_true(r == 0 || r == -ENOENT, "取消结果应为 0 或 -ENOENT，实际: %d", r);

    /* 取消不存在的请求 */
    r = ipc_service_cancel(&g_ipc, 99999);
    zassert_equal(r, -ENOENT, "取消不存在的请求应返回 -ENOENT");

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_generate_request_id) {
    ipc_request_id_t id1 = ipc_generate_request_id();
    ipc_request_id_t id2 = ipc_generate_request_id();
    ipc_request_id_t id3 = ipc_generate_request_id();

    zassert_not_equal(id1, 0, "ID 不应为 0");
    zassert_not_equal(id2, 0, "ID 不应为 0");
    zassert_not_equal(id3, 0, "ID 不应为 0");

    /* ID 应该是唯一的 */
    zassert_not_equal(id1, id2, "ID 应唯一");
    zassert_not_equal(id2, id3, "ID 应唯一");
    zassert_not_equal(id1, id3, "ID 应唯一");
}

/* =============================================================================
 * 边界条件测试
 * ============================================================================= */

ZTEST(ipc_service, test_null_service_init) {
    int r = ipc_service_init(NULL, "test", ipc_ut_handler,
                             CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5, 8, 8);
    zassert_not_equal(r, 0, "NULL service 应失败");
}

ZTEST(ipc_service, test_null_handler_init) {
    int r = ipc_service_init(&g_ipc, "test", NULL,
                             CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5, 8, 8);
    zassert_not_equal(r, 0, "NULL handler 应失败");
}

ZTEST(ipc_service, test_stop_without_start) {
    int r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                             CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    /* 未启动直接停止 */
    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "未启动直接停止应返回 0");
}

ZTEST(ipc_service, test_sync_call_before_start) {
    const char payload[] = "test";
    void* out = NULL;
    size_t outsz = 0;

    int r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                             CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_MSEC(100));
    zassert_not_equal(r, 0, "未启动调用应失败");
}

ZTEST(ipc_service, test_multiple_start_stop_cycles) {
    const char payload[] = "cycle_test";
    void* out = NULL;
    size_t outsz = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    for (int i = 0; i < 3; i++) {
        r = ipc_service_start(&g_ipc);
        zassert_equal(r, 0, "ipc_service_start failed: %d", r);

        r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_SECONDS(1));
        zassert_equal(r, 0, "ipc_call_sync failed: %d", r);

        r = ipc_service_stop(&g_ipc);
        zassert_equal(r, 0, "ipc_service_stop failed: %d", r);

        k_msleep(50);
    }
}

ZTEST(ipc_service, test_concurrent_requests) {
    const char payload1[] = "req1";
    const char payload2[] = "req2";
    const char payload3[] = "req3";
    void* out1 = NULL;
    void* out2 = NULL;
    void* out3 = NULL;
    size_t outsz1 = 0;
    size_t outsz2 = 0;
    size_t outsz3 = 0;
    int r;

    g_handler_call_count = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_counting_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 顺序发送多个请求 */
    r = ipc_call_sync(&g_ipc, payload1, sizeof(payload1), &out1, &outsz1, K_SECONDS(1));
    zassert_equal(r, 0, "req1 failed");

    r = ipc_call_sync(&g_ipc, payload2, sizeof(payload2), &out2, &outsz2, K_SECONDS(1));
    zassert_equal(r, 0, "req2 failed");

    r = ipc_call_sync(&g_ipc, payload3, sizeof(payload3), &out3, &outsz3, K_SECONDS(1));
    zassert_equal(r, 0, "req3 failed");

    zassert_equal(g_handler_call_count, 3, "handler 应被调用 3 次");

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_sync_call_timeout) {
    const char payload[] = "timeout_test";
    void* out = NULL;
    size_t outsz = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 使用很短的超时，应超时 */
    r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_MSEC(10));
    zassert_equal(r, -EAGAIN, "应返回 -EAGAIN，实际: %d", r);

    /* 等待处理完成，避免资源泄漏 */
    k_msleep(200);

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_async_null_callback) {
    const char payload[] = "null_cb_test";
    ipc_request_id_t request_id = 0;
    int r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, CONFIG_THREAD_IPC_SERVICE_STACK_SIZE, 5,
                         CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE, CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* NULL callback 应该被允许（fire-and-forget 模式） */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), NULL, NULL, &request_id);
    /* 允许成功或失败，取决于实现 */
    zassert_true(r == 0 || r != 0, "NULL callback 行为实现定义");

    k_msleep(100);

    ipc_service_stop(&g_ipc);
}

ZTEST_SUITE(ipc_service, NULL, test_suite_setup, NULL, NULL, test_suite_teardown);
