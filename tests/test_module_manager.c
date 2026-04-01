/**
 * @file test_module_manager.c
 * @brief 模块管理器单元测试
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
#include "event_system.h"
#include "module_manager.h"

LOG_MODULE_REGISTER(test_module_manager);

static int stub_init(void* config) {
    (void) config;
    return 0;
}

static int stub_start(void) {
    return 0;
}

static int stub_stop(void) {
    return 0;
}

static void stub_on_event(const event_t* event, void* user_data) {
    (void) event;
    (void) user_data;
}

DECLARE_MODULE_INTERFACE_MINIMAL(stub);

ZTEST(module_manager, test_register_unregister) {
    uint32_t id = 0U;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, "register 失败");
    zassert_true(id > 0U, "module id 应非 0");

    zassert_equal(module_manager_unregister(id), 0, "unregister 失败");
    zassert_equal(module_manager_shutdown(), 0, "shutdown 失败");
}

ZTEST(module_manager, test_get_stats) {
    module_mgr_stats_t stats;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);

    module_manager_get_stats(&stats);
    zassert_equal(stats.total_modules, 0U, "初始应为 0 个模块");

    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST_SUITE(module_manager, NULL, NULL, NULL, NULL, NULL);
