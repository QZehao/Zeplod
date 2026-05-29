/**
 * @file module_manager_registry.c
 * @brief 模块注册表与查询
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include <zephyr/logging/log.h>
#include <string.h>
#include "module_manager_internal.h"

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

module_info_t* find_module_by_id_locked(uint32_t module_id) {
    if (module_id == 0U) {
        return NULL;
    }

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].id == module_id) {
            return &g_module_mgr.modules[i];
        }
    }

    return NULL;
}

uint32_t find_module_id_by_name_locked(const char* name) {
    if (name == NULL) {
        return 0U;
    }

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].interface != NULL && g_module_mgr.modules[i].interface->name != NULL &&
            strcmp(g_module_mgr.modules[i].interface->name, name) == 0) {
            return g_module_mgr.modules[i].id;
        }
    }

    return 0U;
}

uint32_t allocate_unique_module_id(void) {
    for (uint32_t cand = 1U; cand != 0U; cand++) {
        bool taken = false;

        for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
            if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED && g_module_mgr.modules[i].id == cand) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return cand;
        }
    }
    return 0U;
}

void clear_module_slot_locked(module_info_t* info) {
    if (info == NULL) {
        return;
    }

    info->interface = NULL;
    info->config = NULL;
    info->internal_data = NULL;
    info->status = MODULE_STATUS_UNINITIALIZED;
    info->id = 0U;
    info->event_subscription_count = 0U;
    (void) memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));
}

int module_manager_register(const module_interface_t* interface, void* config, uint32_t* module_id) {
    if (!atomic_get(&g_module_mgr_initialized)) {
        LOG_ERR("Module manager not initialized");
        return MODULE_ERR_NOT_INITIALIZED;
    }

    if (interface == NULL) {
        LOG_ERR("NULL interface pointer");
        return MODULE_ERR_INVALID_ARG;
    }

    if (interface->name == NULL || interface->name[0] == '\0') {
        LOG_ERR("Module name is NULL or empty");
        return MODULE_ERR_INVALID_ARG;
    }

    if (interface->init == NULL) {
        LOG_ERR("Module '%s' missing required init function", interface->name);
        return MODULE_ERR_INVALID_ARG;
    }

    module_manager_lock();
    if (g_module_mgr.module_count >= CONFIG_MAX_MODULES) {
        module_manager_unlock();
        LOG_ERR("Maximum module count (%d) reached", CONFIG_MAX_MODULES);
        return MODULE_ERR_NO_MEM;
    }

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED &&
            g_module_mgr.modules[i].interface != NULL && g_module_mgr.modules[i].interface->name != NULL &&
            strcmp(g_module_mgr.modules[i].interface->name, interface->name) == 0) {
            module_manager_unlock();
            LOG_WRN("Module '%s' already registered", interface->name);
            return MODULE_ERR_ALREADY_EXISTS;
        }
    }

    module_info_t* info = NULL;

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].status == MODULE_STATUS_UNINITIALIZED) {
            info = &g_module_mgr.modules[i];
            break;
        }
    }

    if (info == NULL) {
        module_manager_unlock();
        LOG_ERR("No free module slot available");
        return MODULE_ERR_NO_MEM;
    }

    const uint32_t new_id = allocate_unique_module_id();

    if (new_id == 0U) {
        module_manager_unlock();
        LOG_ERR("No free module ID available");
        return MODULE_ERR_NO_MEM;
    }

    info->interface = interface;
    info->config = config;
    info->internal_data = NULL;
    info->status = MODULE_STATUS_INITIALIZING;
    info->id = new_id;
    info->event_subscription_count = 0U;
    (void) memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));

    if (module_id != NULL) {
        *module_id = new_id;
    }

    int (*init_fn)(void*) = interface->init;
    void* cfg = config;

    module_manager_unlock();

    const uint32_t t0 = k_uptime_get_32();
    const int      iret = init_fn(cfg);
    const uint32_t elapsed = k_uptime_get_32() - t0;

    module_manager_lock();

    info = find_module_by_id_locked(new_id);
    if (info == NULL || info->status != MODULE_STATUS_INITIALIZING || info->interface != interface) {
        module_manager_unlock();
        LOG_ERR("Module '%s' slot lost or raced during init", interface->name);
        return MODULE_ERR_IO;
    }

    if (iret != MODULE_OK) {
        LOG_ERR("Module '%s' init failed: %d", interface->name, iret);
        int (*shutdown_fn)(void) = interface->shutdown;
        clear_module_slot_locked(info);
        g_module_mgr.stats.error_modules++;
        module_manager_unlock();
        if (shutdown_fn != NULL) {
            shutdown_fn();
        }
        return iret;
    }

    if (CONFIG_MODULE_INIT_TIMEOUT_MS > 0 && elapsed > (uint32_t) CONFIG_MODULE_INIT_TIMEOUT_MS) {
        int (*shutdown_fn)(void) = interface->shutdown;

        LOG_ERR("Module '%s' init exceeded timeout (%u ms)", interface->name, (unsigned int) elapsed);
        clear_module_slot_locked(info);
        g_module_mgr.stats.error_modules++;
        module_manager_unlock();

        if (shutdown_fn != NULL) {
            shutdown_fn();
        }
        return MODULE_ERR_TIMEOUT;
    }

    info->status = MODULE_STATUS_INITIALIZED;
    g_module_mgr.module_count++;
    g_module_mgr.stats.total_modules++;

    module_manager_unlock();

    LOG_DBG("Module registered: %s (id=%u)", interface->name, (unsigned int) new_id);
    module_mgr_notify_callback(new_id, MODULE_MGR_EVENT_REGISTERED);
    return MODULE_OK;
}

int module_manager_unregister(uint32_t module_id) {
    if (!atomic_get(&g_module_mgr_initialized)) {
        return MODULE_ERR_NOT_INITIALIZED;
    }
    if (module_id == 0U) {
        return MODULE_ERR_INVALID_ARG;
    }

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status == MODULE_STATUS_RUNNING) {
        int (*stop_fn)(void) = NULL;
        int stop_ret = MODULE_OK;

        if (info->interface != NULL) {
            stop_fn = info->interface->stop;
        }
        module_manager_unlock();
        if (stop_fn != NULL) {
            stop_ret = stop_fn();
        }
        module_manager_lock();
        info = find_module_by_id_locked(module_id);
        if (info == NULL) {
            module_manager_unlock();
            return MODULE_ERR_NOT_FOUND;
        }
        if (stop_ret != MODULE_OK) {
            LOG_ERR("Module unregister aborted: stop failed (id=%u ret=%d)", (unsigned int) module_id, stop_ret);
            if (info->status == MODULE_STATUS_RUNNING) {
                info->status = MODULE_STATUS_ERROR;
                g_module_mgr.stats.error_modules++;
            }
            module_manager_unlock();
            module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_ERROR);
            return stop_ret;
        }
        if (info->status == MODULE_STATUS_RUNNING) {
            info->status = MODULE_STATUS_STOPPED;
            if (g_module_mgr.stats.active_modules > 0U) {
                g_module_mgr.stats.active_modules--;
            }
        }
    }

    event_type_t  types[CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS];
    uint32_t      ids[CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS];
    const uint8_t sub_n = module_event_detach_locked(info, types, ids, CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS);

    module_manager_unlock();
    module_event_unsubscribe_batch(types, ids, sub_n);
    module_manager_lock();

    info = find_module_by_id_locked(module_id);
    if (info == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (atomic_get(&g_module_mgr_shutting_down) == 0 && info->interface != NULL && info->interface->shutdown != NULL) {
        int (*sd)(void) = info->interface->shutdown;

        module_manager_unlock();
        sd();
        module_manager_lock();
        info = find_module_by_id_locked(module_id);
        if (info == NULL) {
            module_manager_unlock();
            return MODULE_ERR_NOT_FOUND;
        }
    }

    if (g_module_mgr.module_count > 0U) {
        g_module_mgr.module_count--;
    }
    if (g_module_mgr.stats.total_modules > 0U) {
        g_module_mgr.stats.total_modules--;
    }

    clear_module_slot_locked(info);

    module_manager_unlock();

    LOG_DBG("Module unregistered: id=%d", module_id);
    module_mgr_notify_callback(module_id, MODULE_MGR_EVENT_UNREGISTERED);
    return MODULE_OK;
}

int module_manager_get_module_info(uint32_t module_id, module_info_t* out) {
    if (!atomic_get(&g_module_mgr_initialized) || module_id == 0U || out == NULL) {
        return MODULE_ERR_INVALID_ARG;
    }

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    *out = *info;
    module_manager_unlock();
    return MODULE_OK;
}

uint32_t module_manager_get_id_by_name(const char* name) {
    if (!atomic_get(&g_module_mgr_initialized) || name == NULL) {
        return 0U;
    }

    module_manager_lock();

    const uint32_t id = find_module_id_by_name_locked(name);

    module_manager_unlock();
    return id;
}

void module_manager_foreach(void (*callback)(module_info_t*, void*), void* user_data) {
    if (!atomic_get(&g_module_mgr_initialized) || callback == NULL) {
        return;
    }

    module_info_t snapshot[CONFIG_MAX_MODULES];

    module_manager_lock();

    int n = 0;

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED) {
            snapshot[n++] = g_module_mgr.modules[i];
        }
    }

    module_manager_unlock();

    for (int i = 0; i < n; i++) {
        callback(&snapshot[i], user_data);
    }
}
