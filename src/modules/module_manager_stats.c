/**
 * @file module_manager_stats.c
 * @brief 模块管理器统计、调试与状态回调
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include <zephyr/logging/log.h>
#include <string.h>
#include "module_manager_internal.h"

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

void module_mgr_notify_callback(uint32_t module_id, module_mgr_event_t evt) {
    module_mgr_callback_t cb;
    void*                 ud;

    module_manager_lock();
    cb = g_module_mgr.callback;
    ud = g_module_mgr.callback_user_data;
    module_manager_unlock();

    if (cb != NULL) {
        cb(module_id, evt, ud);
    }
}

void module_manager_get_stats(module_mgr_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    module_manager_lock();
    *stats = g_module_mgr.stats;
    module_manager_unlock();
}

void module_manager_reset_stats(void) {
    module_manager_lock();
    (void) memset(&g_module_mgr.stats, 0, sizeof(g_module_mgr.stats));
    module_manager_unlock();
}

void module_manager_dump_info(void) {
    module_info_t snap[CONFIG_MAX_MODULES];
    uint32_t      mod_count;
    uint32_t      active;
    uint32_t      errors;
    int           n = 0;

    module_manager_lock();

    mod_count = g_module_mgr.module_count;
    active = g_module_mgr.stats.active_modules;
    errors = g_module_mgr.stats.error_modules;

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED) {
            snap[n++] = g_module_mgr.modules[i];
        }
    }

    module_manager_unlock();

    LOG_INF("");
    LOG_INF("=== Module Manager Info ===");
    LOG_INF("Total modules: %u / %d", (unsigned int) mod_count, CONFIG_MAX_MODULES);
    LOG_INF("Active: %u, Errors: %u", (unsigned int) active, (unsigned int) errors);

    for (int i = 0; i < n; i++) {
        module_info_t* info = &snap[i];
        const char*    status_str;

        switch (info->status) {
        case MODULE_STATUS_INITIALIZING:
            status_str = "INITING";
            break;
        case MODULE_STATUS_INITIALIZED:
            status_str = "INIT";
            break;
        case MODULE_STATUS_RUNNING:
            status_str = "RUNNING";
            break;
        case MODULE_STATUS_STOPPED:
            status_str = "STOPPED";
            break;
        case MODULE_STATUS_ERROR:
            status_str = "ERROR";
            break;
        case MODULE_STATUS_SUSPENDED:
            status_str = "SUSPENDED";
            break;
        default:
            status_str = "UNKNOWN";
            break;
        }

        LOG_INF("  [%u] %s - %s (v%u.%u.%u)", (unsigned int) info->id,
                info->interface != NULL && info->interface->name != NULL ? info->interface->name : "N/A", status_str,
                info->interface != NULL ? MODULE_VERSION_MAJOR(info->interface->version) : 0,
                info->interface != NULL ? MODULE_VERSION_MINOR(info->interface->version) : 0,
                info->interface != NULL ? MODULE_VERSION_PATCH(info->interface->version) : 0);
    }

    LOG_INF("=== end ===");
}

void module_manager_set_callback(module_mgr_callback_t callback, void* user_data) {
    module_manager_lock();
    g_module_mgr.callback = callback;
    g_module_mgr.callback_user_data = user_data;
    module_manager_unlock();
}
