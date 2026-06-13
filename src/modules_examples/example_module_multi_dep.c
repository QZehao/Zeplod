/**
 * @file example_module_multi_dep.c
 * @brief 多依赖示例模块实现
 *
 * 依赖名必须与目标模块 interface->name 完全一致（此处为 example_module_a / example_module_b）。
 *
 * depends_on 为直接依赖名列表，以 NULL 结尾；不是依赖链深度。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <zeplod/app_config.h>
#include <zeplod/event_system.h>
#include <zeplod/example_module_multi_dep.h>
#include <zeplod/module_manager.h>

LOG_MODULE_REGISTER(example_module_multi_dep, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 多依赖：静态数组，最后一项必须为 NULL
 * Multiple deps: static array, must end with NULL
 * ============================================================================= */

static const char* const example_module_multi_dep_deps[] = {
    "example_module_a",
    "example_module_b",
    NULL,
};

/* =============================================================================
 * 模块回调（最小实现）
 * ============================================================================= */

static int example_module_multi_dep_init(void* config) {
    (void) config;
    LOG_INF("example_module_multi_dep init (depends on A + B by name)");
    return 0;
}

static int example_module_multi_dep_start(void) {
    LOG_INF("example_module_multi_dep start");
    return 0;
}

static int example_module_multi_dep_stop(void) {
    LOG_INF("example_module_multi_dep stop");
    return 0;
}

static void example_module_multi_dep_on_event(const event_t* event, void* user_data) {
    (void) event;
    (void) user_data;
}

DECLARE_MODULE_INTERFACE_MINIMAL_WITH_DEPS(example_module_multi_dep, example_module_multi_dep_deps);

const module_interface_t* example_module_multi_dep_get_interface(void) {
    return &example_module_multi_dep_interface;
}

static int example_module_multi_dep_auto_register(void) {
    uint32_t module_id;

    if (module_manager_register(example_module_multi_dep_get_interface(), NULL, &module_id) != 0) {
        LOG_ERR("module_manager_register example_module_multi_dep failed");
        return -EIO;
    }
    LOG_INF("Registered example_module_multi_dep (id=%u)", module_id);
    return 0;
}

SYS_INIT(example_module_multi_dep_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_MULTI);
