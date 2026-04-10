/**
 * @file test_sys_timer.c
 * @brief sys_timer 单元测试
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
#include "sys_timer.h"

LOG_MODULE_REGISTER(test_sys_timer);

static void timer_cb(sys_timer_handle_t timer, void* user_data) {
    (void) timer;
    (void) user_data;
}

ZTEST(sys_timer, test_init) {
    zassert_equal(sys_timer_init(), 0, "sys_timer_init 失败");
}

ZTEST(sys_timer, test_create_null_config) {
    zassert_equal(sys_timer_init(), 0, NULL);
    zassert_is_null(sys_timer_create(NULL), "NULL config 应返回 NULL");
}

ZTEST(sys_timer, test_create_and_delete) {
    sys_timer_config_t cfg = {
        .mode = SYS_TIMER_ONESHOT,
        .delay_ms = 200U,
        .period_ms = 0U,
        .callback = timer_cb,
        .user_data = NULL,
        .name = "ut",
        .priority = 5,
    };
    sys_timer_handle_t t;

    zassert_equal(sys_timer_init(), 0, NULL);
    t = sys_timer_create(&cfg);
    zassert_not_null(t, "create 失败");
    zassert_equal(sys_timer_delete(t), 0, "delete 失败");
}

ZTEST(sys_timer, test_delete_null) {
    zassert_equal(sys_timer_init(), 0, NULL);
    zassert_equal(sys_timer_delete(NULL), -EINVAL, "NULL handle 应失败");
}

ZTEST_SUITE(sys_timer, NULL, NULL, NULL, NULL, NULL);
