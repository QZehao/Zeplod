/**
 * @file test_sys_time.c
 * @brief sys_time 服务单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 3 初始版本
 * 2026-06-13       1.1            zeh            对齐 APP_ERR_* 错误码
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>

#include <zeplod/app_config.h>
#include <zeplod/sys_time.h>

ZTEST_SUITE(sys_time_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(sys_time_tests, test_init_idempotent) {
    zassert_equal(sys_time_init(), 0, NULL);
    zassert_equal(sys_time_init(), 0, NULL);
}

ZTEST(sys_time_tests, test_invalid_before_set) {
    int64_t unix_ms = 0;

    zassert_equal(sys_time_init(), 0, NULL);
    sys_time_invalidate();
    zassert_false(sys_time_is_valid(), NULL);
    zassert_equal(sys_time_get_unix_ms(&unix_ms), APP_ERR_TIME, NULL);
}

ZTEST(sys_time_tests, test_get_null_rejected) {
    zassert_equal(sys_time_init(), 0, NULL);
    zassert_equal(sys_time_set_unix_ms(1000000000000LL), 0, NULL);
    zassert_equal(sys_time_get_unix_ms(NULL), APP_ERR_INVALID_PARAM, NULL);
}

ZTEST(sys_time_tests, test_set_get_roundtrip) {
    int64_t unix_ms = 0;
    int64_t base = 1700000000000LL;

    zassert_equal(sys_time_init(), 0, NULL);
    zassert_equal(sys_time_set_unix_ms(base), 0, NULL);
    zassert_true(sys_time_is_valid(), NULL);

    k_sleep(K_MSEC(5));
    zassert_equal(sys_time_get_unix_ms(&unix_ms), 0, NULL);
    zassert_true(unix_ms >= base, "unix_ms=%lld base=%lld", (long long) unix_ms, (long long) base);
    zassert_true(unix_ms <= base + 50LL, "unix_ms=%lld", (long long) unix_ms);
}

ZTEST(sys_time_tests, test_reject_negative_timestamp) {
    zassert_equal(sys_time_init(), 0, NULL);
    zassert_equal(sys_time_set_unix_ms(-1), APP_ERR_INVALID_PARAM, NULL);
}

ZTEST(sys_time_tests, test_invalidate) {
    zassert_equal(sys_time_init(), 0, NULL);
    zassert_equal(sys_time_set_unix_ms(1000000000000LL), 0, NULL);
    sys_time_invalidate();
    zassert_false(sys_time_is_valid(), NULL);
}
