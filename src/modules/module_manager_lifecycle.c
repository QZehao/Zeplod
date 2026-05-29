/**
 * @file module_manager_lifecycle.c
 * @brief 模块管理器与单模块生命周期
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include "module_manager_internal.h"
#include "state_machine.h"

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

int module_manager_init(void) {
    LOG_DBG("Initializing module manager...");

    if (atomic_get(&g_module_mgr_initialized)) {
        LOG_WRN("Module manager already initialized");
        return MODULE_ERR_ALREADY_EXISTS;
    }

    (void) memset(&g_module_mgr, 0, sizeof(g_module_mgr));
    k_mutex_init(&g_module_mgr.lock);
    zepl_state_machine_init(&g_module_mgr.lifecycle, ZEP_STATE_UNINIT);

    atomic_set(&g_module_mgr_shutting_down, 0);

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        g_module_mgr.modules[i].status = MODULE_STATUS_UNINITIALIZED;
        g_module_mgr.modules[i].id = 0U;
    }

    (void) zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_INITED);
    g_module_mgr.initialized = true;
    atomic_set(&g_module_mgr_initialized, 1);

    LOG_DBG("Module manager initialized");
    return MODULE_OK;
}

int module_manager_start(void) {
    if (!atomic_get(&g_module_mgr_initialized)) {
        LOG_ERR("Module manager not initialized");
        return MODULE_ERR_NOT_INITIALIZED;
    }

    module_manager_lock();
    zepl_state_t state = module_manager_lifecycle_state_locked();

    if (state == ZEP_STATE_RUNNING) {
        module_manager_unlock();
        LOG_WRN("Module manager already running");
        return MODULE_ERR_ALREADY_RUNNING;
    }

    if (state == ZEP_STATE_UNINIT || state == ZEP_STATE_ERROR || state == ZEP_STATE_STOPPING) {
        module_manager_unlock();
        LOG_ERR("Module manager is not in a startable state: %s", zepl_state_name(state));
        return MODULE_ERR_INVALID_ARG;
    }

    if (zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_STARTING) != 0 ||
        zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_RUNNING) != 0) {
        module_manager_unlock();
        LOG_ERR("Failed to transition module manager to RUNNING from %s", zepl_state_name(state));
        return MODULE_ERR_INVALID_ARG;
    }

    g_module_mgr.running = true;
    module_manager_unlock();

    LOG_DBG("Module manager started");
    return MODULE_OK;
}

int module_manager_stop(void) {
    if (!atomic_get(&g_module_mgr_initialized)) {
        LOG_ERR("Module manager not initialized");
        return MODULE_ERR_NOT_INITIALIZED;
    }

    module_manager_lock();
    zepl_state_t state = module_manager_lifecycle_state_locked();

    if (state == ZEP_STATE_UNINIT) {
        module_manager_unlock();
        LOG_ERR("Module manager not initialized");
        return MODULE_ERR_NOT_INITIALIZED;
    }

    if (state == ZEP_STATE_STOPPED) {
        g_module_mgr.running = false;
        module_manager_unlock();
        return MODULE_OK;
    }

    if (state == ZEP_STATE_ERROR) {
        module_manager_unlock();
        LOG_ERR("Module manager is in error state");
        return MODULE_ERR_INVALID_ARG;
    }

    if (state != ZEP_STATE_STOPPING) {
        if (zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_STOPPING) != 0) {
            module_manager_unlock();
            LOG_ERR("Failed to transition module manager to STOPPING from %s", zepl_state_name(state));
            return MODULE_ERR_INVALID_ARG;
        }
    }

    g_module_mgr.running = false;
    module_manager_unlock();

    (void) module_manager_stop_all();

    module_manager_lock();
    (void) zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_STOPPED);
    module_manager_unlock();

    LOG_DBG("Module manager stopped");
    return MODULE_OK;
}

int module_manager_shutdown(void) {
    start_order_entry_t entries[CONFIG_MAX_MODULES];
    int                 n = 0;

    LOG_DBG("Shutting down module manager...");

    if (!atomic_get(&g_module_mgr_initialized)) {
        LOG_ERR("Module manager not initialized");
        return MODULE_ERR_NOT_INITIALIZED;
    }

    atomic_set(&g_module_mgr_shutting_down, 1);

    (void) module_manager_stop();

    module_manager_lock();

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        module_info_t* info = &g_module_mgr.modules[i];

        if (info->event_subscription_count > 0U) {
            event_type_t  types[CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS];
            uint32_t      ids[CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS];
            const uint8_t sub_n = module_event_detach_locked(info, types, ids, CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS);

            module_manager_unlock();
            module_event_unsubscribe_batch(types, ids, sub_n);
            module_manager_lock();
            info = &g_module_mgr.modules[i];
        }

        if (info->status != MODULE_STATUS_UNINITIALIZED && info->interface != NULL &&
            info->interface->shutdown != NULL) {
            entries[n].id = info->id;
            entries[n].priority = info->interface->priority;
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
            entries[n].depends_on = info->interface->depends_on;
            entries[n].depends_version_min = info->interface->depends_version_min;
#endif
            n++;
        }
    }

    module_manager_unlock();

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    n = mm_dep_planner_build_stop_order(entries, n);
#else
    mm_dep_planner_sort_priority_asc(entries, n);
    for (int i = 0; i < n / 2; i++) {
        const int           j = n - 1 - i;
        start_order_entry_t t = entries[i];

        entries[i] = entries[j];
        entries[j] = t;
    }
#endif

    if (n > 0) {
        LOG_DBG("Calling shutdown for %d modules", n);
    }

    for (int i = 0; i < n; i++) {
        int (*shutdown_fn)(void) = NULL;

        module_manager_lock();

        module_info_t* info = find_module_by_id_locked(entries[i].id);

        if (info != NULL && info->interface != NULL) {
            shutdown_fn = info->interface->shutdown;
        }

        module_manager_unlock();

        if (shutdown_fn != NULL) {
            const int ret = shutdown_fn();

            if (ret != MODULE_OK) {
                LOG_WRN("Module shutdown id=%u returned %d", (unsigned int) entries[i].id, ret);
            }
        }
    }

    module_manager_lock();

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        clear_module_slot_locked(&g_module_mgr.modules[i]);
    }

    g_module_mgr.module_count = 0U;
    (void) memset(&g_module_mgr.stats, 0, sizeof(g_module_mgr.stats));
    (void) zepl_state_machine_try_transition(&g_module_mgr.lifecycle, ZEP_STATE_UNINIT);
    g_module_mgr.initialized = false;
    atomic_set(&g_module_mgr_initialized, 0);
    g_module_mgr.running = false;

    module_manager_unlock();

    atomic_set(&g_module_mgr_shutting_down, 0);

    LOG_DBG("Module manager shutdown complete");
    return MODULE_OK;
}

int module_manager_start_module(uint32_t module_id) {
    int (*start_fn)(void);
    char        name_buf[MM_MODULE_NAME_MAX];
    const char* name;
    int         ret = MODULE_OK;

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status != MODULE_STATUS_INITIALIZED && info->status != MODULE_STATUS_STOPPED) {
        module_manager_unlock();
        return MODULE_ERR_INVALID_ARG;
    }

    start_fn = info->interface->start;
    mm_copy_module_name(name_buf, info->interface->name);
    name = name_buf;

    module_manager_unlock();

    if (start_fn != NULL) {
        ret = start_fn();
    }

    module_manager_lock();

    info = find_module_by_id_locked(module_id);
    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (ret != MODULE_OK) {
        LOG_ERR("Module '%s' start failed: %d", name != NULL ? name : "?", ret);
        info->status = MODULE_STATUS_ERROR;
        g_module_mgr.stats.error_modules++;
        module_manager_unlock();
        module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_ERROR);
        return ret;
    }

    info->status = MODULE_STATUS_RUNNING;
    g_module_mgr.stats.active_modules++;

    module_manager_unlock();

    LOG_DBG("Module started: %s", name != NULL ? name : "?");
    module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_STARTED);
    return MODULE_OK;
}

int module_manager_stop_module(uint32_t module_id) {
    int (*stop_fn)(void);
    int         ret = MODULE_OK;
    char        name_buf[MM_MODULE_NAME_MAX];
    const char* name;

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status != MODULE_STATUS_RUNNING) {
        module_manager_unlock();
        return MODULE_OK;
    }

    stop_fn = info->interface->stop;
    mm_copy_module_name(name_buf, info->interface->name);
    name = name_buf;

    module_manager_unlock();

    if (stop_fn != NULL) {
        ret = stop_fn();
    }

    module_manager_lock();

    info = find_module_by_id_locked(module_id);
    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (ret != MODULE_OK) {
        LOG_ERR("Module '%s' stop failed: %d", name != NULL ? name : "?", ret);
        if (info->status == MODULE_STATUS_RUNNING) {
            info->status = MODULE_STATUS_ERROR;
            g_module_mgr.stats.error_modules++;
        }
        module_manager_unlock();
        module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_ERROR);
        return ret;
    }

    if (info->status == MODULE_STATUS_RUNNING) {
        info->status = MODULE_STATUS_STOPPED;
        if (g_module_mgr.stats.active_modules > 0U) {
            g_module_mgr.stats.active_modules--;
        }
    }

    module_manager_unlock();

    LOG_DBG("Module stopped: %s", name != NULL ? name : "?");
    module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_STOPPED);
    return MODULE_OK;
}

int module_manager_start_all(void) {
    start_order_entry_t entries[CONFIG_MAX_MODULES];
    int                 n = 0;

    module_manager_lock();

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        module_info_t* m = &g_module_mgr.modules[i];

        if ((m->status == MODULE_STATUS_INITIALIZED || m->status == MODULE_STATUS_STOPPED) && m->interface != NULL) {
            entries[n].id = m->id;
            entries[n].priority = m->interface->priority;
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
            entries[n].depends_on = m->interface->depends_on;
            entries[n].depends_version_min = m->interface->depends_version_min;
#endif
            n++;
        }
    }

    module_manager_unlock();

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    n = mm_dep_planner_build_start_order(entries, n);
#else
    mm_dep_planner_sort_priority_asc(entries, n);
#endif

    int started = 0;

    for (int i = 0; i < n; i++) {
        const int ret = module_manager_start_module(entries[i].id);

        if (ret == 0) {
            started++;
        } else if (IS_ENABLED(CONFIG_MODULE_MANAGER_START_ALL_ABORT_ON_FAILURE)) {
            break;
        }
    }

    return started;
}

int module_manager_stop_all(void) {
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    start_order_entry_t entries[CONFIG_MAX_MODULES];
#else
    uint32_t ids[CONFIG_MAX_MODULES];
#endif
    int n = 0;

    module_manager_lock();

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        module_info_t* const m = &g_module_mgr.modules[i];

        if (m->status == MODULE_STATUS_RUNNING && m->interface != NULL) {
            entries[n].id = m->id;
            entries[n].priority = m->interface->priority;
            entries[n].depends_on = m->interface->depends_on;
            entries[n].depends_version_min = m->interface->depends_version_min;
            n++;
        }
    }
#else
    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].status == MODULE_STATUS_RUNNING) {
            ids[n++] = g_module_mgr.modules[i].id;
        }
    }
#endif

    module_manager_unlock();

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    (void) mm_dep_planner_build_stop_order(entries, n);
#endif

    int stopped = 0;

    for (int i = 0; i < n; i++) {
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
        if (module_manager_stop_module(entries[i].id) == 0) {
#else
        if (module_manager_stop_module(ids[i]) == 0) {
#endif
            stopped++;
        }
    }

    return stopped;
}

int module_manager_suspend_module(uint32_t module_id) {
    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status != MODULE_STATUS_RUNNING) {
        module_manager_unlock();
        return MODULE_ERR_INVALID_ARG;
    }

    if (g_module_mgr.stats.active_modules > 0U) {
        g_module_mgr.stats.active_modules--;
    }
    info->status = MODULE_STATUS_SUSPENDED;
    char        name_buf[MM_MODULE_NAME_MAX];
    const char* name;

    mm_copy_module_name(name_buf, info->interface->name);
    name = name_buf;

    module_manager_unlock();

    LOG_DBG("Module suspended: %s", name[0] != '\0' ? name : "?");
    module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_STATUS_CHANGED);
    return MODULE_OK;
}

int module_manager_resume_module(uint32_t module_id) {
    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status != MODULE_STATUS_SUSPENDED) {
        module_manager_unlock();
        return MODULE_ERR_INVALID_ARG;
    }

    info->status = MODULE_STATUS_RUNNING;
    g_module_mgr.stats.active_modules++;
    char        name_buf[MM_MODULE_NAME_MAX];
    const char* name;

    mm_copy_module_name(name_buf, info->interface->name);
    name = name_buf;

    module_manager_unlock();

    LOG_DBG("Module resumed: %s", name[0] != '\0' ? name : "?");
    module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_STATUS_CHANGED);
    return MODULE_OK;
}
