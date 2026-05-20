/**
 * @file module_manager_compat.c
 * @brief 模块管理器兼容层实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 *
 */

#include "module_manager_compat.h"
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>
#include "app_config.h"

LOG_MODULE_REGISTER(module_manager_compat, CONFIG_SYS_LOG_LEVEL);

#if MODULE_COMPAT_USE_PRO
#include "module_manager_pro.h"

static int module_compat_map_pro_err(int pr) {
    return pr == MODULE_MANAGER_PRO_OK ? 0 : pr;
}

#else
#include "module_manager.h"
#endif

int module_compat_init(const module_compat_config_t* config) {
#if MODULE_COMPAT_USE_PRO
    module_manager_pro_config_t pro_config;

    (void) memset(&pro_config, 0, sizeof(pro_config));

    if (config != NULL) {
        /* 字段语义映射（compat 配置 → PRO Kconfig/结构） */
        pro_config.enable_hot_reload = config->enable_hotplug;
        pro_config.enable_health_check = config->enable_health_monitor;
        pro_config.enable_auto_recovery = config->enable_auto_deps;
        pro_config.enable_statistics = config->enable_lifecycle_hooks;
        pro_config.health_check_interval_ms = 1000U;
        pro_config.max_restart_count = (config->max_dependencies != 0U) ? (uint32_t) config->max_dependencies : 3U;
    } else {
        pro_config.enable_hot_reload = false;
        pro_config.enable_health_check = true;
        pro_config.enable_auto_recovery = true;
        pro_config.enable_statistics = true;
        pro_config.health_check_interval_ms = 1000U;
        pro_config.max_restart_count = 3U;
    }

    int ret = module_manager_pro_init(&pro_config);
    return module_compat_map_pro_err(ret);
#else
    int ret = module_manager_init();

    if (ret != 0) {
        LOG_ERR("Failed to init module_manager: %d", ret);
        return ret;
    }
    LOG_INF("Module manager (standard) initialized");
    return 0;
#endif
}

int module_compat_start(void) {
#if MODULE_COMPAT_USE_PRO
    return module_compat_map_pro_err(module_manager_pro_start_all());
#else
    int ret = module_manager_start();

    if (ret != 0) {
        LOG_ERR("Failed to start module_manager");
        return ret;
    }
    return 0;
#endif
}

int module_compat_stop(void) {
#if MODULE_COMPAT_USE_PRO
    return module_compat_map_pro_err(module_manager_pro_stop_all());
#else
    int ret = module_manager_stop();

    if (ret != 0) {
        LOG_ERR("Failed to stop module_manager");
        return ret;
    }
    return 0;
#endif
}

int module_compat_shutdown(void) {
#if MODULE_COMPAT_USE_PRO
    module_manager_pro_deinit();
    return 0;
#else
    int ret = module_manager_shutdown();

    if (ret != 0) {
        LOG_ERR("Failed to shutdown module_manager");
        return ret;
    }
    return 0;
#endif
}

void module_compat_get_stats(module_compat_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    (void) memset(stats, 0, sizeof(*stats));

#if MODULE_COMPAT_USE_PRO
    module_manager_pro_stats_t pro_stats = {0};

    if (module_manager_pro_get_stats(&pro_stats) != MODULE_MANAGER_PRO_OK) {
        return;
    }

    stats->total_modules = pro_stats.total_modules;
    stats->active_modules = pro_stats.running_modules;
    stats->error_modules = pro_stats.error_modules;
    stats->events_processed = 0U;
    stats->events_dropped = 0U;
    stats->hotplug_events = pro_stats.total_updates;
    stats->dependency_resolutions = 0U;
    stats->health_check_cycles = 0U;
#else
    module_mgr_stats_t std_stats = {0};

    module_manager_get_stats(&std_stats);

    stats->total_modules = std_stats.total_modules;
    stats->active_modules = std_stats.active_modules;
    stats->error_modules = std_stats.error_modules;
    stats->events_processed = std_stats.events_processed;
    stats->events_dropped = std_stats.events_dropped;
#endif
}

void module_compat_reset_stats(void) {
#if MODULE_COMPAT_USE_PRO
    (void) module_manager_pro_reset_stats();
#else
    module_manager_reset_stats();
#endif
}

#if MODULE_COMPAT_USE_PRO

static int module_compat_pro_std_only(const char* api_name) {
    LOG_WRN("%s: use module_manager_pro_* APIs in PRO build", api_name);
    return -ENOTSUP;
}

int module_compat_register(const module_interface_t* interface, void* config, uint32_t* module_id) {
    ARG_UNUSED(interface);
    ARG_UNUSED(config);
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_register");
}

int module_compat_unregister(uint32_t module_id) {
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_unregister");
}

int module_compat_get_module_info(uint32_t module_id, module_info_t* out) {
    ARG_UNUSED(module_id);
    ARG_UNUSED(out);
    return module_compat_pro_std_only("module_compat_get_module_info");
}

uint32_t module_compat_get_id_by_name(const char* name) {
    ARG_UNUSED(name);
    (void) module_compat_pro_std_only("module_compat_get_id_by_name");
    return 0U;
}

int module_compat_foreach(void (*callback)(module_info_t*, void*), void* user_data) {
    ARG_UNUSED(callback);
    ARG_UNUSED(user_data);
    return module_compat_pro_std_only("module_compat_foreach");
}

int module_compat_start_module(uint32_t module_id) {
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_start_module");
}

int module_compat_stop_module(uint32_t module_id) {
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_stop_module");
}

int module_compat_suspend_module(uint32_t module_id) {
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_suspend_module");
}

int module_compat_resume_module(uint32_t module_id) {
    ARG_UNUSED(module_id);
    return module_compat_pro_std_only("module_compat_resume_module");
}

int module_compat_subscribe(uint32_t module_id, event_type_t event_type) {
    ARG_UNUSED(module_id);
    ARG_UNUSED(event_type);
    return module_compat_pro_std_only("module_compat_subscribe");
}

int module_compat_unsubscribe(uint32_t module_id, event_type_t event_type) {
    ARG_UNUSED(module_id);
    ARG_UNUSED(event_type);
    return module_compat_pro_std_only("module_compat_unsubscribe");
}

int module_compat_send_to_module(uint32_t module_id, const event_t* event) {
    ARG_UNUSED(module_id);
    ARG_UNUSED(event);
    return module_compat_pro_std_only("module_compat_send_to_module");
}

int module_compat_broadcast(const event_t* event) {
    ARG_UNUSED(event);
    return module_compat_pro_std_only("module_compat_broadcast");
}

int module_compat_set_callback(module_mgr_callback_t callback, void* user_data) {
    ARG_UNUSED(callback);
    ARG_UNUSED(user_data);
    LOG_WRN("module_compat_set_callback: use module_manager_pro_register_state_callback with PRO");
    return -ENOTSUP;
}

int module_compat_start_all(void) {
    return module_compat_map_pro_err(module_manager_pro_start_all());
}

int module_compat_stop_all(void) {
    return module_compat_map_pro_err(module_manager_pro_stop_all());
}

void module_compat_dump_info(void) {
    module_manager_pro_print_module_list();
}

#endif /* MODULE_COMPAT_USE_PRO */

/* =============================================================================
 * SYS_INIT 自动注册
 * ============================================================================= */

static int module_compat_auto_register(void) {
    int ret = module_compat_init(NULL);

    if (ret != 0) {
        LOG_ERR("Failed to init module manager compat");
        return -EIO;
    }

    ret = module_compat_start();
    if (ret != 0) {
        LOG_ERR("Failed to start module manager compat");
        return -EIO;
    }

    LOG_INF("Module manager compat initialized and started");
    return 0;
}

SYS_INIT(module_compat_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_MGR);
