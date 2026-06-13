/**
 * @file test_app_kv_migrate.c
 * @brief app_kv schema 迁移钩子单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 */

#include <zephyr/ztest.h>

#include <zeplod/app_kv.h>

ZTEST_SUITE(app_kv_migrate_tests, NULL, NULL, NULL, NULL, NULL);

static int g_migrate_called;

static int migrate_stub(uint32_t from_ver, uint32_t to_ver, void* user_data) {
    int* called = (int*) user_data;

    zassert_equal(from_ver, 1U, NULL);
    zassert_equal(to_ver, 2U, NULL);
    zassert_not_null(called, NULL);
    (*called)++;
    return 0;
}

static void setup_kv(void) {
    app_kv_init();
    g_migrate_called = 0;
}

ZTEST(app_kv_migrate_tests, test_migrate_callback) {
    setup_kv();

    zassert_equal(app_kv_register_migrate(1U, 2U, migrate_stub, &g_migrate_called), APP_OK, NULL);
    zassert_equal(app_kv_set_schema_version(1U), APP_OK, NULL);
    zassert_equal(app_kv_run_migrations(), APP_OK, NULL);
    zassert_equal(g_migrate_called, 1, NULL);
    zassert_equal(app_kv_get_schema_version(), 2U, NULL);
}

ZTEST(app_kv_migrate_tests, test_migrate_chain) {
    setup_kv();

    zassert_equal(app_kv_register_migrate(1U, 2U, migrate_stub, &g_migrate_called), APP_OK, NULL);
    zassert_equal(app_kv_set_schema_version(1U), APP_OK, NULL);
    zassert_equal(app_kv_run_migrations(), APP_OK, NULL);
    zassert_equal(app_kv_run_migrations(), APP_OK, NULL);
    zassert_equal(g_migrate_called, 1, NULL);
    zassert_equal(app_kv_get_schema_version(), 2U, NULL);
}

ZTEST(app_kv_migrate_tests, test_register_invalid_range) {
    setup_kv();
    zassert_equal(app_kv_register_migrate(2U, 2U, migrate_stub, &g_migrate_called), APP_ERR_INVALID_PARAM, NULL);
    zassert_equal(app_kv_register_migrate(3U, 1U, migrate_stub, &g_migrate_called), APP_ERR_INVALID_PARAM, NULL);
}

ZTEST(app_kv_migrate_tests, test_register_duplicate) {
    setup_kv();
    zassert_equal(app_kv_register_migrate(1U, 2U, migrate_stub, &g_migrate_called), APP_OK, NULL);
    zassert_equal(app_kv_register_migrate(1U, 2U, migrate_stub, &g_migrate_called), APP_ERR_ALREADY_EXISTS, NULL);
}
