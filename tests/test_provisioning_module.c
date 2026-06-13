/**
 * @file test_provisioning_module.c
 * @brief provisioning_module 单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 3 初始版本
 * 2026-06-13       1.1            zeh            事件订阅用例
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/event_dispatcher.h>
#include <zeplod/event_system.h>
#include <zeplod/provisioning_module.h>

/* =============================================================================
 * 测试夹具
 * ============================================================================= */

static void setup_provisioning(void) {
    zassert_equal(provisioning_module_init(NULL), 0, NULL);
    zassert_equal(provisioning_module_init(NULL), 0, "init idempotent");
    zassert_equal(provisioning_module_start(), 0, NULL);
}

static void teardown_provisioning(void) {
    (void) provisioning_module_stop();
    (void) provisioning_module_shutdown();
}

static void* provisioning_suite_setup(void) {
    int ret;

    ret = event_system_init();
    zassert_true(ret == EVENT_OK || ret == -EALREADY, "event_system_init: %d", ret);
    ret = event_system_start();
    zassert_true(ret == EVENT_OK || ret == -EALREADY, "event_system_start: %d", ret);
    ret = event_dispatcher_init(NULL);
    zassert_true(ret == EVENT_OK || ret == -EALREADY, "event_dispatcher_init: %d", ret);
    ret = event_dispatcher_start();
    zassert_true(ret == EVENT_OK || ret == -EALREADY, "event_dispatcher_start: %d", ret);

    return NULL;
}

ZTEST_SUITE(provisioning_module_tests, NULL, provisioning_suite_setup, NULL, NULL, NULL);

/* =============================================================================
 * 事件回调
 * ============================================================================= */

static volatile provisioning_state_t g_last_prov_state;
static volatile int                  g_prov_event_count;
static struct k_sem                  g_prov_sem;

static void provisioning_state_event_cb(const event_t* ev, void* user_data) {
    const provisioning_status_t* st;

    (void) user_data;
    if (ev->type != EVENT_PROVISIONING_STATE_CHANGED || ev->data_len < sizeof(provisioning_status_t)) {
        return;
    }
    st = (const provisioning_status_t*) ev->data.inline_data;
    g_last_prov_state = st->state;
    g_prov_event_count++;
    k_sem_give(&g_prov_sem);
}

/* =============================================================================
 * 用例
 * ============================================================================= */

ZTEST(provisioning_module_tests, test_begin_stub_success) {
    provisioning_state_t state;

    setup_provisioning();

    zassert_equal(provisioning_module_get_state(&state), 0, NULL);
    zassert_equal(state, PROVISIONING_STATE_UNPROVISIONED, NULL);

    zassert_equal(provisioning_module_begin(NULL), 0, NULL);
    zassert_equal(provisioning_module_get_state(&state), 0, NULL);
    zassert_equal(state, PROVISIONING_STATE_PROVISIONED, NULL);

    zassert_equal(provisioning_module_begin(NULL), APP_ERR_PROVISIONING, "already provisioned");

    teardown_provisioning();
}

ZTEST(provisioning_module_tests, test_begin_publishes_events) {
    uint32_t sub_id;

    k_sem_init(&g_prov_sem, 0, 10);
    g_last_prov_state = PROVISIONING_STATE_UNPROVISIONED;
    g_prov_event_count = 0;

    zassert_equal(event_subscribe(EVENT_PROVISIONING_STATE_CHANGED, provisioning_state_event_cb, NULL, &sub_id),
                  EVENT_OK);

    setup_provisioning();
    zassert_equal(provisioning_module_begin(NULL), 0, NULL);

    /* IN_PROGRESS + PROVISIONED */
    zassert_equal(k_sem_take(&g_prov_sem, K_SECONDS(2)), 0, "first prov event");
    zassert_equal(k_sem_take(&g_prov_sem, K_SECONDS(2)), 0, "second prov event");
    zassert_true(g_prov_event_count >= 2, "event count=%d", g_prov_event_count);
    zassert_equal(g_last_prov_state, PROVISIONING_STATE_PROVISIONED, NULL);

    teardown_provisioning();
    event_unsubscribe(EVENT_PROVISIONING_STATE_CHANGED, sub_id);
}

ZTEST(provisioning_module_tests, test_reset_and_device_id) {
    provisioning_state_t state;
    char                 dev_id[64];

    setup_provisioning();

    zassert_equal(provisioning_module_begin(NULL), 0, NULL);
    zassert_equal(provisioning_module_reset(), 0, NULL);
    zassert_equal(provisioning_module_get_state(&state), 0, NULL);
    zassert_equal(state, PROVISIONING_STATE_UNPROVISIONED, NULL);

    zassert_equal(provisioning_module_get_device_id(dev_id, sizeof(dev_id)), 0, NULL);
    zassert_true(strlen(dev_id) > 0U, NULL);
    zassert_mem_equal(dev_id, CONFIG_PROVISIONING_DEVICE_ID, strlen(CONFIG_PROVISIONING_DEVICE_ID), NULL);

    teardown_provisioning();
}

ZTEST(provisioning_module_tests, test_begin_without_start_rejected) {
    zassert_equal(provisioning_module_init(NULL), 0, NULL);
    zassert_equal(provisioning_module_begin(NULL), APP_ERR_INIT, NULL);
    (void) provisioning_module_shutdown();
}

ZTEST(provisioning_module_tests, test_get_state_null_rejected) {
    setup_provisioning();
    zassert_equal(provisioning_module_get_state(NULL), APP_ERR_INVALID_PARAM, NULL);
    teardown_provisioning();
}

ZTEST(provisioning_module_tests, test_module_lifecycle) {
    zassert_equal(provisioning_module_init(NULL), 0, NULL);
    zassert_equal(provisioning_module_get_status(), MODULE_STATUS_INITIALIZED, NULL);
    zassert_equal(provisioning_module_start(), 0, NULL);
    zassert_equal(provisioning_module_get_status(), MODULE_STATUS_RUNNING, NULL);
    zassert_equal(provisioning_module_stop(), 0, NULL);
    zassert_equal(provisioning_module_shutdown(), 0, NULL);
    zassert_equal(provisioning_module_get_status(), MODULE_STATUS_UNINITIALIZED, NULL);
}
