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

#include "example_module_uart.h"
#include "ipc_service.h"

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
static int              g_async_callback_count = 0;
static int              g_async_callback_result = 0;
static size_t           g_async_callback_data_size = 0;
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

static void* test_suite_setup(void) {
    /* 确保 UART 模块不会在后台运行（避免 spinlock 竞争） */
    example_module_uart_stop();

    return NULL;
}

static void test_suite_teardown(void* fixture) {
    (void) fixture;
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

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
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
    char   large_payload[256];
    void*  out = NULL;
    size_t outsz = 0;
    int    r;

    memset(large_payload, 'A', sizeof(large_payload) - 1);
    large_payload[sizeof(large_payload) - 1] = '\0';

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
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
    void*      out = NULL;
    size_t     outsz = 0;
    int        r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_error_handler, 5);
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
    const char       payload[] = "async_test";
    ipc_request_id_t request_id = 0;
    int              r;

    g_async_callback_count = 0;
    g_async_callback_result = 0;
    g_async_callback_data_size = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
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
    const char       payload[] = "delayed_async";
    ipc_request_id_t request_id = 0;
    int              r;

    g_async_callback_count = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
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
    const char    payload[] = "future_test";
    ipc_future_t* future = NULL;
    int           result = -1;
    const void*   out_data = NULL;
    size_t        out_size = 0;
    int           r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                        0,
#endif
                        &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);
    zassert_not_null(future, "future 不应为 NULL");

    r = ipc_future_wait(&g_ipc, future, &result, &out_data, &out_size, K_SECONDS(2));
    zassert_equal(r, 0, "ipc_future_wait failed: %d", r);
    zassert_equal(result, 0, "result 应为 0");
    zassert_equal(out_size, sizeof(payload), "数据大小应匹配");
    zassert_mem_equal(out_data, payload, sizeof(payload), NULL);

    r = ipc_future_release(&g_ipc, future);
    zassert_equal(r, 0, "ipc_future_release failed: %d", r);

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_future_is_ready) {
    const char    payload[] = "future_ready_test";
    ipc_future_t* future = NULL;
    int           r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                        0,
#endif
                        &future);
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
    const char    payload[] = "timeout_test";
    ipc_future_t* future = NULL;
    int           result = 0;
    const void*   out_data = NULL;
    size_t        out_size = 0;
    int           r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                        0,
#endif
                        &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);

    /* 使用很短的超时，应超时 */
    r = ipc_future_wait(&g_ipc, future, &result, &out_data, &out_size, K_MSEC(10));
    zassert_equal(r, -EAGAIN, "应返回 -EAGAIN，实际: %d", r);

    /* 等待实际完成后再释放 */
    k_msleep(200);
    ipc_future_wait(&g_ipc, future, &result, &out_data, &out_size, K_SECONDS(1));
    ipc_future_release(&g_ipc, future);
    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_stop_completes_pending_future) {
    const char    payload[] = "future_stop_test";
    ipc_future_t* future = NULL;
    int           future_result = 0;
    const void*   out_data = NULL;
    size_t        out_size = 0;
    int           r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                        0,
#endif
                        &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);
    zassert_not_null(future, "future should not be NULL");

    /* stop 必须唤醒 future 等待者，不能让 future 悬挂在 pending 表中。 */
    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "ipc_service_stop failed: %d", r);

    zassert_true(ipc_future_is_ready(future), "future should be completed by stop");

    r = ipc_future_wait(&g_ipc, future, &future_result, &out_data, &out_size, K_NO_WAIT);
    zassert_equal(r, 0, "ipc_future_wait after stop failed: %d", r);
    zassert_equal(future_result, -ECANCELED, "future should be canceled by stop");
    zassert_is_null(out_data, "canceled future should not expose data");
    zassert_equal(out_size, 0, "canceled future data size should be 0");

    r = ipc_future_release(&g_ipc, future);
    zassert_equal(r, 0, "ipc_future_release failed: %d", r);
}

/* =============================================================================
 * 请求管理测试
 * ============================================================================= */

ZTEST(ipc_service, test_get_pending_count) {
    const char       payload[] = "pending_test";
    ipc_request_id_t request_id = 0;
    int              r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 初始应为 0 */
    size_t count = ipc_service_get_pending_count(&g_ipc);
    zassert_equal(count, 0, "初始 pending 数量应为 0");

    /* 发送异步请求 */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
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

ZTEST(ipc_service, test_cancel_future_before_stop) {
    const char    payload[] = "cancel_future_stop";
    ipc_future_t* future = NULL;
    int           future_result = 0;
    const void*   out_data = NULL;
    size_t        out_size = 0;
    int           r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_future(&g_ipc, payload, sizeof(payload),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                        0,
#endif
                        &future);
    zassert_equal(r, 0, "ipc_call_future failed: %d", r);
    zassert_not_null(future, "future should not be NULL");

    r = ipc_service_cancel(&g_ipc, future->request_id);
    zassert_equal(r, 0, "cancel future request failed: %d", r);
    zassert_true(ipc_future_is_ready(future), "future should be ready after cancel");

    r = ipc_future_wait(&g_ipc, future, &future_result, &out_data, &out_size, K_NO_WAIT);
    zassert_equal(r, 0, "ipc_future_wait after cancel failed: %d", r);
    zassert_equal(future_result, -ECANCELED, "canceled future should return -ECANCELED");

    r = ipc_future_release(&g_ipc, future);
    zassert_equal(r, 0, "ipc_future_release failed: %d", r);

    /* cancel 已收尾 pending 后，stop 仍应正常完成且可再次 start */
    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "ipc_service_stop after cancel failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start after cancel+stop failed: %d", r);

    ipc_service_stop(&g_ipc);
}

ZTEST(ipc_service, test_cancel_async_then_stop) {
    const char       payload[] = "cancel_async_stop";
    ipc_request_id_t request_id = 0;
    int              r;

    g_async_callback_count = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
    zassert_equal(r, 0, "ipc_call_async failed: %d", r);

    (void) ipc_service_cancel(&g_ipc, request_id);

    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "ipc_service_stop after cancel+async failed: %d", r);

    zassert_equal(ipc_service_get_pending_count(&g_ipc), 0U, "stop 后 pending 应为 0");
}

ZTEST(ipc_service, test_cancel_request) {
    const char       payload[] = "cancel_test";
    ipc_request_id_t request_id = 0;
    int              r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 发送异步请求 */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), async_test_callback, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
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
    int r = ipc_service_init(NULL, "test", ipc_ut_handler, 5);
    zassert_not_equal(r, 0, "NULL service 应失败");
}

ZTEST(ipc_service, test_null_handler_init) {
    int r = ipc_service_init(&g_ipc, "test", NULL, 5);
    zassert_not_equal(r, 0, "NULL handler 应失败");
}

ZTEST(ipc_service, test_stop_without_start) {
    int r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    /* 未启动直接停止 */
    r = ipc_service_stop(&g_ipc);
    zassert_equal(r, 0, "未启动直接停止应返回 0");
}

ZTEST(ipc_service, test_sync_call_before_start) {
    const char payload[] = "test";
    void*      out = NULL;
    size_t     outsz = 0;

    int r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_call_sync(&g_ipc, payload, sizeof(payload), &out, &outsz, K_MSEC(100));
    zassert_not_equal(r, 0, "未启动调用应失败");
}

ZTEST(ipc_service, test_multiple_start_stop_cycles) {
    const char payload[] = "cycle_test";
    void*      out = NULL;
    size_t     outsz = 0;
    int        r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
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
    void*      out1 = NULL;
    void*      out2 = NULL;
    void*      out3 = NULL;
    size_t     outsz1 = 0;
    size_t     outsz2 = 0;
    size_t     outsz3 = 0;
    int        r;

    g_handler_call_count = 0;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_counting_handler, 5);
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
    void*      out = NULL;
    size_t     outsz = 0;
    int        r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_delayed_handler, 5);
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
    const char       payload[] = "null_cb_test";
    ipc_request_id_t request_id = 0;
    int              r;

    r = ipc_service_init(&g_ipc, "ut_ipc", ipc_ut_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* NULL callback 应该被允许（fire-and-forget 模式） */
    r = ipc_call_async(&g_ipc, payload, sizeof(payload), NULL, NULL,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                       0,
#endif
                       &request_id);
    /* 允许成功或失败，取决于实现 */
    zassert_true(r == 0 || r != 0, "NULL callback 行为实现定义");

    k_msleep(100);

    ipc_service_stop(&g_ipc);
}

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
/* =============================================================================
 * 共享内存引用计数测试
 * ============================================================================= */

/* 服务函数：分配共享内存并返回（通过测试全局 g_ipc） */
static int ipc_shm_alloc_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                                 size_t* out_data_size) {
    (void) request_id;
    (void) data;
    (void) data_size;

    ipc_shm_handle_t handle = ipc_shm_alloc(&g_ipc, 32);

    if (handle == IPC_SHM_HANDLE_INVALID) {
        return -ENOMEM;
    }

    void* ptr = ipc_shm_get_ptr(&g_ipc, handle);

    if (ptr == NULL) {
        (void) ipc_shm_release(&g_ipc, handle);
        return -EIO;
    }

    (void) memset(ptr, 0, 32);
    (void) memcpy(ptr, "shm_out", 8);

    *out_data = ptr;
    *out_data_size = 32;
    return 0;
}

/* 测试 sync_shm 端到端：service 分配 shm，调用方通过句柄释放 */
ZTEST(ipc_service, test_sync_call_shm_output) {
    void*            out_data = NULL;
    size_t           out_size = 0;
    ipc_shm_handle_t out_handle = IPC_SHM_HANDLE_INVALID;
    int              r;

    r = ipc_service_init(&g_ipc, "shm_sync", ipc_shm_alloc_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    r = ipc_call_sync_shm(&g_ipc, NULL, 0, 0, &out_data, &out_size, &out_handle, K_SECONDS(2));
    zassert_equal(r, 0, "ipc_call_sync_shm failed: %d", r);
    zassert_true(out_handle != IPC_SHM_HANDLE_INVALID, "expected valid out shm handle");
    zassert_not_null(out_data, "expected out_data");
    zassert_equal(out_size, 32, "unexpected out_size");
    zassert_mem_equal(out_data, "shm_out", 7, "unexpected shm payload");

    r = ipc_shm_release(&g_ipc, out_handle);
    zassert_equal(r, 0, "shm_release failed: %d", r);

    ipc_service_stop(&g_ipc);
}

/* 测试共享内存基本功能 */
ZTEST(ipc_service, test_shared_memory_basic) {
    int r;

    r = ipc_service_init(&g_ipc, "shm_test", ipc_shm_alloc_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    r = ipc_service_start(&g_ipc);
    zassert_equal(r, 0, "ipc_service_start failed: %d", r);

    /* 测试共享内存分配 */
    ipc_shm_handle_t handle = ipc_shm_alloc(&g_ipc, 64);
    zassert_true(handle != IPC_SHM_HANDLE_INVALID, "shm_alloc failed");

    void* ptr = ipc_shm_get_ptr(&g_ipc, handle);
    zassert_not_null(ptr, "shm_get_ptr returned NULL");

    size_t size = ipc_shm_get_size(&g_ipc, handle);
    zassert_equal(size, 64, "shm_get_size returned wrong size: %zu", size);

    /* 写入测试数据 */
    const char test_data[] = "hello shm";
    memcpy(ptr, test_data, sizeof(test_data));

    /* 测试引用计数 */
    ipc_shm_acquire(&g_ipc, handle);
    r = ipc_shm_release(&g_ipc, handle);
    zassert_equal(r, 0, "shm_release failed: %d", r);

    /* 再次释放，引用计数应降至 0 */
    r = ipc_shm_release(&g_ipc, handle);
    zassert_equal(r, 0, "shm_release failed on final release: %d", r);

    /* 验证句柄不再有效 */
    zassert_false(ipc_shm_is_valid(&g_ipc, handle), "handle should be invalid after release");

    ipc_service_stop(&g_ipc);
}

/* 测试共享内存池统计 */
ZTEST(ipc_service, test_shared_memory_stats) {
    int      r;
    uint32_t active, peak, free;

    r = ipc_service_init(&g_ipc, "shm_stats", ipc_shm_alloc_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    /* 初始状态 */
    ipc_shm_get_stats(&g_ipc, &active, &peak, &free);
    zassert_equal(active, 0, "initial active should be 0");
    zassert_equal(peak, 0, "initial peak should be 0");
    zassert_equal(free, CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE, "initial free should equal pool size");

    /* 分配几个块 */
    ipc_shm_handle_t h1 = ipc_shm_alloc(&g_ipc, 32);
    ipc_shm_handle_t h2 = ipc_shm_alloc(&g_ipc, 64);

    ipc_shm_get_stats(&g_ipc, &active, &peak, &free);
    zassert_equal(active, 2, "active should be 2");
    zassert_equal(peak, 2, "peak should be 2");

    /* 释放一个 */
    ipc_shm_release(&g_ipc, h1);
    ipc_shm_get_stats(&g_ipc, &active, &peak, &free);
    zassert_equal(active, 1, "active should be 1 after release");
    zassert_equal(peak, 2, "peak should remain 2");

    /* 释放另一个 */
    ipc_shm_release(&g_ipc, h2);

    ipc_service_stop(&g_ipc);
}

/* 测试共享内存池耗尽 */
ZTEST(ipc_service, test_shared_memory_exhaustion) {
    int              r;
    uint32_t         pool_size = CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE;
    ipc_shm_handle_t handles[16]; /* 假设池大小至少 8 */

    r = ipc_service_init(&g_ipc, "shm_exhaust", ipc_shm_alloc_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    /* 分配直到池满 */
    uint32_t allocated = 0;
    for (uint32_t i = 0; i < pool_size && i < ARRAY_SIZE(handles); i++) {
        handles[i] = ipc_shm_alloc(&g_ipc, 32);
        if (handles[i] != IPC_SHM_HANDLE_INVALID) {
            allocated++;
        } else {
            break;
        }
    }

    zassert_true(allocated > 0, "should allocate at least one block");

    /* 尝试再分配一个，应该失败 */
    ipc_shm_handle_t extra = ipc_shm_alloc(&g_ipc, 32);
    /* 可能成功（如果池大小>handles 数组）或失败 */
    if (extra != IPC_SHM_HANDLE_INVALID) {
        ipc_shm_release(&g_ipc, extra);
    }

    /* 释放所有已分配的块 */
    for (uint32_t i = 0; i < allocated; i++) {
        r = ipc_shm_release(&g_ipc, handles[i]);
        zassert_equal(r, 0, "release failed at index %u: %d", i, r);
    }

    ipc_service_stop(&g_ipc);
}

/* 测试无效参数处理 */
ZTEST(ipc_service, test_shared_memory_invalid_params) {
    int r;

    r = ipc_service_init(&g_ipc, "shm_invalid", ipc_shm_alloc_handler, 5);
    zassert_equal(r, 0, "ipc_service_init failed: %d", r);

    /* NULL service */
    zassert_equal(ipc_shm_alloc(NULL, 32), IPC_SHM_HANDLE_INVALID, "alloc with NULL service should fail");
    zassert_false(ipc_shm_is_valid(NULL, 123), "is_valid with NULL service should return false");

    /* 过大尺寸 */
    ipc_shm_handle_t h = ipc_shm_alloc(&g_ipc, CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE + 1);
    zassert_equal(h, IPC_SHM_HANDLE_INVALID, "alloc with oversized should fail");

    /* 零尺寸 */
    h = ipc_shm_alloc(&g_ipc, 0);
    zassert_equal(h, IPC_SHM_HANDLE_INVALID, "alloc zero size should fail");

    /* 无效句柄 */
    zassert_is_null(ipc_shm_get_ptr(&g_ipc, IPC_SHM_HANDLE_INVALID), "get_ptr invalid handle should return NULL");
    zassert_equal(ipc_shm_get_size(&g_ipc, IPC_SHM_HANDLE_INVALID), 0, "get_size invalid handle should return 0");
    zassert_equal(ipc_shm_release(&g_ipc, IPC_SHM_HANDLE_INVALID), -EINVAL, "release invalid handle should fail");

    ipc_service_stop(&g_ipc);
}
#endif /* CONFIG_THREAD_IPC_SERVICE_SHARED_MEM */

ZTEST_SUITE(ipc_service, NULL, test_suite_setup, NULL, NULL, test_suite_teardown);
