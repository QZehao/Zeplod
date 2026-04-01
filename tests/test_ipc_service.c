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
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "ipc_service.h"

LOG_MODULE_REGISTER(test_ipc_service);

static ipc_service_t g_ipc;

static int ipc_ut_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                          size_t* out_data_size) {
    (void) request_id;
    *out_data = (void*) data;
    *out_data_size = data_size;
    return 0;
}

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

ZTEST_SUITE(ipc_service, NULL, NULL, NULL, NULL, NULL);
