/**
 * @file module_manager_compat.c
 * @brief 模块管理器兼容层实现
 */

#include "module_manager_compat.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include "module_manager.h"

LOG_MODULE_REGISTER(module_manager_compat, CONFIG_SYS_LOG_LEVEL);

#if MODULE_COMPAT_USE_PRO
#include "module_manager_pro.h"
#endif

int module_compat_init(const module_compat_config_t* config) {
#if MODULE_COMPAT_USE_PRO
    module_manager_pro_config_t pro_config = {0};

    if (config != NULL) {
        pro_config.max_modules = config->max_modules;
        pro_config.max_dependencies = config->max_dependencies;
        pro_config.enable_auto_deps = config->enable_auto_deps;
        pro_config.enable_hotplug = config->enable_hotplug;
        pro_config.enable_lifecycle_hooks = config->enable_lifecycle_hooks;
        pro_config.enable_health_monitor = config->enable_health_monitor;
    } else {
        pro_config.max_modules = 64;
        pro_config.max_dependencies = 16;
        pro_config.enable_auto_deps = true;
        pro_config.enable_hotplug = false;
        pro_config.enable_lifecycle_hooks = true;
        pro_config.enable_health_monitor = true;
    }

    int ret = module_manager_pro_init(&pro_config);
    if (ret != MODULE_MANAGER_PRO_OK) {
        LOG_ERR("Failed to init module_manager_pro: %d", ret);
        return -1;
    }
    LOG_INF("Module manager PRO initialized");
    return 0;
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
    int ret = module_manager_pro_start();
    if (ret != MODULE_MANAGER_PRO_OK) {
        LOG_ERR("Failed to start module_manager_pro");
        return -1;
    }
#else
    int ret = module_manager_start();
    if (ret != 0) {
        LOG_ERR("Failed to start module_manager");
        return ret;
    }
#endif
    return 0;
}

int module_compat_stop(void) {
#if MODULE_COMPAT_USE_PRO
    int ret = module_manager_pro_stop();
    if (ret != MODULE_MANAGER_PRO_OK) {
        LOG_ERR("Failed to stop module_manager_pro");
        return -1;
    }
#else
    int ret = module_manager_stop();
    if (ret != 0) {
        LOG_ERR("Failed to stop module_manager");
        return ret;
    }
#endif
    return 0;
}

int module_compat_shutdown(void) {
#if MODULE_COMPAT_USE_PRO
    int ret = module_manager_pro_shutdown();
    if (ret != MODULE_MANAGER_PRO_OK) {
        LOG_ERR("Failed to shutdown module_manager_pro");
        return -1;
    }
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

    memset(stats, 0, sizeof(*stats));

#if MODULE_COMPAT_USE_PRO
    module_manager_pro_stats_t pro_stats = {0};
    module_manager_pro_get_stats(&pro_stats);

    stats->total_modules = pro_stats.total_modules;
    stats->active_modules = pro_stats.active_modules;
    stats->error_modules = pro_stats.error_modules;
    stats->events_processed = pro_stats.events_processed;
    stats->events_dropped = pro_stats.events_dropped;
    stats->hotplug_events = pro_stats.hotplug_events;
    stats->dependency_resolutions = pro_stats.dependency_resolutions;
    stats->health_check_cycles = pro_stats.health_check_cycles;
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
    module_manager_pro_reset_stats();
#else
    module_manager_reset_stats();
#endif
}
