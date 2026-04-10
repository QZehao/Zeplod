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

ZTEST(module_manager, test_start_all_stop_all) {
    uint32_t id1 = 0U, id2 = 0U;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    /* 注册多个模块 */
    zassert_equal(module_manager_register(&stub_interface, NULL, &id1), 0, "register 1 失败");
    zassert_equal(module_manager_register(&stub_interface, NULL, &id2), 0, "register 2 失败");

    /* 启动所有模块 */
    int started = module_manager_start_all();
    zassert_true(started >= 2, "应至少启动 2 个模块");

    /* 停止所有模块 */
    int stopped = module_manager_stop_all();
    zassert_true(stopped >= 2, "应至少停止 2 个模块");

    zassert_equal(module_manager_unregister(id1), 0, NULL);
    zassert_equal(module_manager_unregister(id2), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_suspend_resume) {
    uint32_t id = 0U;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, "register 失败");

    /* 启动模块 */
    zassert_equal(module_manager_start_module(id), 0, "start_module 失败");

    /* 暂停模块 */
    zassert_equal(module_manager_suspend_module(id), 0, "suspend 失败");

    /* 恢复模块 */
    zassert_equal(module_manager_resume_module(id), 0, "resume 失败");

    /* 停止并注销 */
    zassert_equal(module_manager_stop_module(id), 0, "stop_module 失败");
    zassert_equal(module_manager_unregister(id), 0, "unregister 失败");
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_get_module_info) {
    uint32_t id = 0U;
    module_info_t info;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, "register 失败");

    /* 获取模块信息 */
    zassert_equal(module_manager_get_module_info(id, &info), 0, "get_module_info 失败");
    zassert_equal(info.id, id, "模块 ID 应匹配");
    zassert_not_null(info.name, "模块名称不应为 NULL");

    /* 测试无效 ID */
    zassert_equal(module_manager_get_module_info(9999, &info), -1, "无效 ID 应返回 -1");

    /* 测试 NULL 输出 */
    zassert_equal(module_manager_get_module_info(id, NULL), -1, "NULL 输出应返回 -1");

    zassert_equal(module_manager_unregister(id), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_get_id_by_name) {
    uint32_t id = 0U;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, "register 失败");

    /* 按名称获取 ID */
    uint32_t found_id = module_manager_get_id_by_name("stub");
    zassert_equal(found_id, id, "按名称查找的 ID 应匹配");

    /* 测试不存在的名称 */
    uint32_t invalid_id = module_manager_get_id_by_name("nonexistent");
    zassert_equal(invalid_id, 0U, "不存在的名称应返回 0");

    zassert_equal(module_manager_unregister(id), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_foreach) {
    static int count = 0;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    /* 注册几个模块 */
    uint32_t id1 = 0U, id2 = 0U, id3 = 0U;
    zassert_equal(module_manager_register(&stub_interface, NULL, &id1), 0, NULL);
    zassert_equal(module_manager_register(&stub_interface, NULL, &id2), 0, NULL);
    zassert_equal(module_manager_register(&stub_interface, NULL, &id3), 0, NULL);

    /* 遍历回调 */
    void count_callback(module_info_t* info, void* user_data) {
        (void)info;
        (void)user_data;
        count++;
    }

    count = 0;
    module_manager_foreach(count_callback, NULL);
    zassert_equal(count, 3, "应遍历到 3 个模块");

    zassert_equal(module_manager_unregister(id1), 0, NULL);
    zassert_equal(module_manager_unregister(id2), 0, NULL);
    zassert_equal(module_manager_unregister(id3), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_reset_stats) {
    module_mgr_stats_t stats;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);

    /* 注册和注销模块会增加 events_processed */
    uint32_t id = 0U;
    module_manager_register(&stub_interface, NULL, &id);
    module_manager_unregister(id);

    /* 重置统计 */
    module_manager_reset_stats();
    module_manager_get_stats(&stats);
    zassert_equal(stats.total_modules, 0U, "重置后 total_modules 应为 0");

    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_dump_info) {
    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    /* 注册一个模块 */
    uint32_t id = 0U;
    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, NULL);

    /* 调用 dump（不应崩溃）*/
    module_manager_dump_info();

    zassert_equal(module_manager_unregister(id), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST(module_manager, test_set_callback) {
    static int callback_count = 0;

    zassert_equal(event_system_init(), EVENT_OK, NULL);
    zassert_equal(module_manager_init(), 0, NULL);
    zassert_equal(module_manager_start(), 0, NULL);

    /* 设置回调 */
    void mgr_callback(uint32_t module_id, module_mgr_event_t event, void* user_data) {
        (void)module_id;
        (void)event;
        (void)user_data;
        callback_count++;
    }

    module_manager_set_callback(mgr_callback, NULL);

    /* 注册模块应触发回调 */
    uint32_t id = 0U;
    zassert_equal(module_manager_register(&stub_interface, NULL, &id), 0, NULL);

    k_msleep(20);
    zassert_true(callback_count > 0, "回调应被调用");

    zassert_equal(module_manager_unregister(id), 0, NULL);
    zassert_equal(module_manager_shutdown(), 0, NULL);
}

ZTEST_SUITE(module_manager, NULL, NULL, NULL, NULL, NULL);
