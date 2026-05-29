/**
 * @file test_concurrency_stress.c
 * @brief 核心并发压力测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-29
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "event_dispatcher.h"
#include "event_system.h"
#include "module_manager.h"
#include "ztest_sync.h"

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE)
#include "ipc_service.h"
#endif

LOG_MODULE_REGISTER(test_concurrency_stress);

#define STRESS_EVENT_TYPE        220U
#define STRESS_EVENT_PRODUCERS   3
#define STRESS_EVENT_PER_THREAD  10
#define STRESS_READER_THREADS    3
#define STRESS_READER_ITERATIONS 32

static struct k_sem g_stress_event_sem;
static atomic_t     g_stress_event_count;

static void stress_event_handler(const event_t* event, void* user_data) {
    ARG_UNUSED(event);
    ARG_UNUSED(user_data);

    atomic_inc(&g_stress_event_count);
    k_sem_give(&g_stress_event_sem);
}

static K_THREAD_STACK_ARRAY_DEFINE(g_event_producer_stacks, STRESS_EVENT_PRODUCERS, 1024);
static struct k_thread g_event_producer_threads[STRESS_EVENT_PRODUCERS];

static void stress_event_producer(void* p1, void* p2, void* p3) {
    uintptr_t producer_id = (uintptr_t) p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (uint32_t i = 0; i < STRESS_EVENT_PER_THREAD; i++) {
        uint32_t payload = (uint32_t) (producer_id * 100U + i);
        (void) event_publish_copy((event_type_t) STRESS_EVENT_TYPE, EVENT_PRIORITY_NORMAL, &payload, sizeof(payload));
        k_yield();
    }
}

ZTEST(concurrency_stress, test_event_multi_producer_publish) {
    uint32_t subscriber_id = 0U;

    k_sem_init(&g_stress_event_sem, 0, K_SEM_MAX_LIMIT);
    atomic_set(&g_stress_event_count, 0);

    (void) event_system_shutdown();
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_init(NULL), EVENT_OK, NULL);
    zassert_equal(event_register_type((event_type_t) STRESS_EVENT_TYPE, "stress_event"), EVENT_OK, NULL);
    zassert_equal(event_subscribe((event_type_t) STRESS_EVENT_TYPE, stress_event_handler, NULL, &subscriber_id),
                  EVENT_OK, NULL);
    zassert_equal(event_system_start(), EVENT_OK, NULL);
    zassert_equal(event_dispatcher_start(), EVENT_OK, NULL);

    for (uintptr_t i = 0; i < STRESS_EVENT_PRODUCERS; i++) {
        k_tid_t tid = k_thread_create(&g_event_producer_threads[i], g_event_producer_stacks[i],
                                      K_THREAD_STACK_SIZEOF(g_event_producer_stacks[i]), stress_event_producer,
                                      (void*) i, NULL, NULL, 5, 0, K_NO_WAIT);
        k_thread_name_set(tid, "evt_stress_pub");
    }

    for (uint32_t i = 0; i < STRESS_EVENT_PRODUCERS; i++) {
        zassert_equal(k_thread_join(&g_event_producer_threads[i], K_SECONDS(2)), 0, NULL);
    }

    for (uint32_t i = 0; i < STRESS_EVENT_PRODUCERS * STRESS_EVENT_PER_THREAD; i++) {
        zassert_true(ztest_wait_sem(&g_stress_event_sem, K_SECONDS(2)), "all stress events should dispatch");
    }

    zassert_equal(atomic_get(&g_stress_event_count), STRESS_EVENT_PRODUCERS * STRESS_EVENT_PER_THREAD, NULL);

    zassert_equal(event_dispatcher_stop(), EVENT_OK, NULL);
    zassert_equal(event_system_shutdown(), EVENT_OK, NULL);
}

static K_THREAD_STACK_ARRAY_DEFINE(g_module_reader_stacks, STRESS_READER_THREADS, 1024);
static struct k_thread g_module_reader_threads[STRESS_READER_THREADS];
static atomic_t        g_module_reader_errors;

static void stress_module_reader(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (uint32_t i = 0; i < STRESS_READER_ITERATIONS; i++) {
        module_mgr_stats_t stats;

        module_manager_get_stats(&stats);
        (void) module_manager_get_id_by_name("stress_missing_module");
        k_yield();
    }
}

ZTEST(concurrency_stress, test_module_manager_concurrent_readers) {
    atomic_set(&g_module_reader_errors, 0);
    zassert_equal(module_manager_init(), 0, NULL);

    for (uintptr_t i = 0; i < STRESS_READER_THREADS; i++) {
        k_tid_t tid = k_thread_create(&g_module_reader_threads[i], g_module_reader_stacks[i],
                                      K_THREAD_STACK_SIZEOF(g_module_reader_stacks[i]), stress_module_reader, (void*) i,
                                      NULL, NULL, 5, 0, K_NO_WAIT);
        k_thread_name_set(tid, "mod_stress_rd");
    }

    for (uint32_t i = 0; i < STRESS_READER_THREADS; i++) {
        zassert_equal(k_thread_join(&g_module_reader_threads[i], K_SECONDS(2)), 0, NULL);
    }

    zassert_equal(atomic_get(&g_module_reader_errors), 0, NULL);
}

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE)

#define STRESS_IPC_CLIENTS          2
#define STRESS_IPC_CALLS_PER_CLIENT 4

static ipc_service_t g_stress_ipc;
static atomic_t      g_stress_ipc_handler_count;
static atomic_t      g_stress_ipc_errors;

static int stress_ipc_handler(ipc_request_id_t request_id, const void* data, size_t data_size, void** out_data,
                              size_t* out_data_size) {
    ARG_UNUSED(request_id);

    atomic_inc(&g_stress_ipc_handler_count);
    *out_data = (void*) data;
    *out_data_size = data_size;
    return 0;
}

static K_THREAD_STACK_ARRAY_DEFINE(g_ipc_client_stacks, STRESS_IPC_CLIENTS, 2048);
static struct k_thread g_ipc_client_threads[STRESS_IPC_CLIENTS];

static void stress_ipc_client(void* p1, void* p2, void* p3) {
    uintptr_t client_id = (uintptr_t) p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (uint32_t i = 0; i < STRESS_IPC_CALLS_PER_CLIENT; i++) {
        uint32_t payload = (uint32_t) (client_id * 100U + i);
        void*    out = NULL;
        size_t   out_size = 0U;
        int      ret = ipc_call_sync(&g_stress_ipc, &payload, sizeof(payload), &out, &out_size, K_SECONDS(2));

        if (ret != 0 || out_size != sizeof(payload)) {
            atomic_inc(&g_stress_ipc_errors);
        }
        k_yield();
    }
}

ZTEST(concurrency_stress, test_ipc_multi_client_sync_calls) {
    atomic_set(&g_stress_ipc_handler_count, 0);
    atomic_set(&g_stress_ipc_errors, 0);

    zassert_equal(ipc_service_init(&g_stress_ipc, "stress_ipc", stress_ipc_handler, 5), 0, NULL);
    zassert_equal(ipc_service_start(&g_stress_ipc), 0, NULL);

    for (uintptr_t i = 0; i < STRESS_IPC_CLIENTS; i++) {
        k_tid_t tid = k_thread_create(&g_ipc_client_threads[i], g_ipc_client_stacks[i],
                                      K_THREAD_STACK_SIZEOF(g_ipc_client_stacks[i]), stress_ipc_client, (void*) i, NULL,
                                      NULL, 5, 0, K_NO_WAIT);
        k_thread_name_set(tid, "ipc_stress_cli");
    }

    for (uint32_t i = 0; i < STRESS_IPC_CLIENTS; i++) {
        zassert_equal(k_thread_join(&g_ipc_client_threads[i], K_SECONDS(4)), 0, NULL);
    }

    zassert_equal(atomic_get(&g_stress_ipc_errors), 0, NULL);
    zassert_equal(atomic_get(&g_stress_ipc_handler_count), STRESS_IPC_CLIENTS * STRESS_IPC_CALLS_PER_CLIENT, NULL);
    zassert_equal(ipc_service_get_pending_count(&g_stress_ipc), 0, NULL);
    zassert_equal(ipc_service_stop(&g_stress_ipc), 0, NULL);
}

#endif /* CONFIG_THREAD_IPC_SERVICE */

ZTEST_SUITE(concurrency_stress, NULL, NULL, NULL, NULL, NULL);
