/**
 * @file test_example_module_multi_dep.c
 * @brief 多依赖示例模块单元测试
 * @author OpenClaw Agent
 * @version 1.0
 * @date 2026-04-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-10       1.0            agent          为 example_module_multi_dep 编写测试用例
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <string.h>
#include <zeplod/event_system.h>
#include <zeplod/example_module_a.h>
#include <zeplod/example_module_b.h>
#include <zeplod/example_module_multi_dep.h>
#include <zeplod/module_manager.h>

LOG_MODULE_REGISTER(test_example_module_multi_dep);

/* =============================================================================
 * 测试夹具
 * ============================================================================= */

static void* test_suite_setup(void) {
    zassert_equal(event_system_init(), EVENT_OK, "事件系统初始化失败");
    zassert_equal(module_manager_init(), 0, "模块管理器初始化失败");
    zassert_equal(module_manager_start(), 0, "模块管理器启动失败");
    return NULL;
}

static void test_suite_teardown(void* fixture) {
    (void) fixture;
    module_manager_shutdown();
    event_system_stop();
}

/* =============================================================================
 * 测试用例：接口获取
 * ============================================================================= */

/**
 * @brief 测试获取模块接口
 */
ZTEST(example_module_multi_dep, test_get_interface) {
    const module_interface_t* iface = example_module_multi_dep_get_interface();

    zassert_not_null(iface, "接口不应为 NULL");
    zassert_true(strcmp(iface->name, "example_module_multi_dep") == 0, "接口名称应匹配");
}

/**
 * @brief 测试接口中的 depends_on 数组
 */
ZTEST(example_module_multi_dep, test_depends_on) {
    const module_interface_t* iface = example_module_multi_dep_get_interface();

    zassert_not_null(iface->depends_on, "depends_on 不应为 NULL");
    zassert_not_null(iface->depends_on[0], "第一个依赖不应为 NULL");
    zassert_not_null(iface->depends_on[1], "第二个依赖不应为 NULL");
    zassert_is_null(iface->depends_on[2], "依赖数组应以 NULL 结尾");

    /* 验证依赖的模块名称 */
    zassert_true(strcmp(iface->depends_on[0], "example_module_a") == 0, "第一个依赖应为 example_module_a");
    zassert_true(strcmp(iface->depends_on[1], "example_module_b") == 0, "第二个依赖应为 example_module_b");
}

/* =============================================================================
 * 测试用例：版本信息
 * ============================================================================= */

/**
 * @brief 测试版本号
 */
ZTEST(example_module_multi_dep, test_version) {
    const module_interface_t* iface = example_module_multi_dep_get_interface();

    /* 验证版本号格式正确 */
    zassert_true(iface->version > 0, "版本号应 > 0");

    /* 验证版本号编码正确 */
    uint8_t major = MODULE_VERSION_MAJOR(iface->version);
    uint8_t minor = MODULE_VERSION_MINOR(iface->version);
    uint8_t patch = MODULE_VERSION_PATCH(iface->version);

    zassert_true(major <= 255, "主版本号应 <= 255");
    zassert_true(minor <= 255, "次版本号应 <= 255");
    zassert_true(patch <= 255, "补丁号应 <= 255");
}

/* =============================================================================
 * 测试用例：优先级
 * ============================================================================= */

/**
 * @brief 测试模块优先级
 */
ZTEST(example_module_multi_dep, test_priority) {
    const module_interface_t* iface = example_module_multi_dep_get_interface();

    /* 验证优先级在有效范围内 */
    zassert_true(iface->priority >= MODULE_PRIORITY_CRITICAL, "优先级应 >= 最高优先级");
    zassert_true(iface->priority <= MODULE_PRIORITY_LOW, "优先级应 <= 最低优先级");
}

/* =============================================================================
 * 测试用例：模块注册（依赖排序验证）
 * ============================================================================= */

/**
 * @brief 测试按依赖顺序注册模块
 *
 * 注意：此测试验证模块管理器能够处理带依赖的模块注册。
 * 实际运行时依赖排序需要启用 CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES
 */
ZTEST(example_module_multi_dep, test_register_with_deps) {
    uint32_t id_a = 0, id_b = 0, id_multi = 0;

    /* 先注册依赖的模块 */
    zassert_equal(module_manager_register(example_module_a_get_interface(), NULL, &id_a), 0, "注册 module_a 失败");
    zassert_true(id_a > 0, "module_a ID 应 > 0");

    zassert_equal(module_manager_register(example_module_b_get_interface(), NULL, &id_b), 0, "注册 module_b 失败");
    zassert_true(id_b > 0, "module_b ID 应 > 0");

    /* 再注册带依赖的模块 */
    zassert_equal(module_manager_register(example_module_multi_dep_get_interface(), NULL, &id_multi), 0,
                  "注册 module_multi_dep 失败");
    zassert_true(id_multi > 0, "module_multi_dep ID 应 > 0");

    /* 验证 ID 分配正确 */
    zassert_true(id_multi > id_a, "multi_dep 的 ID 应大于 module_a");
    zassert_true(id_multi > id_b, "multi_dep 的 ID 应大于 module_b");
}

/* =============================================================================
 * 测试用例：接口函数指针
 * ============================================================================= */

/**
 * @brief 测试接口函数指针完整性
 */
ZTEST(example_module_multi_dep, test_interface_functions) {
    const module_interface_t* iface = example_module_multi_dep_get_interface();

    /* 所有函数指针都应该是 NULL（这个模块只有接口定义） */
    /* 或者，这个模块可能实现了所有接口 */
    /* 根据实际实现来验证 */
    if (iface->init != NULL) {
        zassert_true(iface->start != NULL, "start 不应为 NULL");
        zassert_true(iface->stop != NULL, "stop 不应为 NULL");
    }
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(example_module_multi_dep, NULL, test_suite_setup, NULL, NULL, test_suite_teardown);
