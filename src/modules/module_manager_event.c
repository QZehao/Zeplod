/**
 * @file module_manager_event.c
 * @brief 模块事件订阅与分发
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include "module_manager_internal.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include "event_system.h"

LOG_MODULE_DECLARE(module_manager, CONFIG_SYS_LOG_LEVEL);

static void module_event_handler(const event_t* event, void* user_data);

uint8_t module_event_detach_locked(module_info_t* info, event_type_t* types_out, uint32_t* ids_out, uint8_t max_out) {
    if (info == NULL || types_out == NULL || ids_out == NULL || max_out == 0U) {
        return 0U;
    }

    uint8_t n = info->event_subscription_count;
    if (n > max_out) {
        n = max_out;
    }

    for (uint8_t i = 0U; i < n; i++) {
        types_out[i] = info->event_subscriptions[i].type;
        ids_out[i] = info->event_subscriptions[i].subscriber_id;
    }

    info->event_subscription_count = CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS;
    (void) memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));
    return n;
}

void module_event_unsubscribe_batch(const event_type_t* types, const uint32_t* ids, uint8_t count) {
    if (types == NULL || ids == NULL) {
        return;
    }

    for (uint8_t i = 0U; i < count; i++) {
        (void) event_unsubscribe(types[i], ids[i]);
    }
}

int find_event_sub_index(const module_info_t* info, event_type_t type) {
    for (uint8_t i = 0; i < info->event_subscription_count; i++) {
        if (info->event_subscriptions[i].type == type) {
            return (int) i;
        }
    }
    return -1;
}

int event_status_to_module_error(event_status_t status) {
    switch (status) {
    case EVENT_OK:
        return MODULE_OK;
    case EVENT_ERR_NO_MEM:
        return MODULE_ERR_NO_MEM;
    case EVENT_ERR_INVALID_ARG:
        return MODULE_ERR_INVALID_ARG;
    case EVENT_ERR_NOT_FOUND:
        return MODULE_ERR_NOT_FOUND;
    case EVENT_ERR_NO_SUBSCRIBER:
        return MODULE_ERR_NOT_FOUND;
    case EVENT_ERR_TIMEOUT:
        return MODULE_ERR_TIMEOUT;
    case EVENT_ERR_NOT_RUNNING:
        return MODULE_ERR_NOT_RUNNING;
    case EVENT_ERR_QUEUE_FULL:
    case EVENT_ERR_QUEUE_EMPTY:
    default:
        return MODULE_ERR_IO;
    }
}

int module_manager_subscribe(uint32_t module_id, event_type_t event_type) {
    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL || info->interface->on_event == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (find_event_sub_index(info, event_type) >= 0) {
        module_manager_unlock();
        return MODULE_ERR_BUSY;
    }

    if (info->event_subscription_count >= CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS) {
        module_manager_unlock();
        return MODULE_ERR_NO_MEM;
    }

    module_manager_unlock();

    uint32_t             subscriber_id = 0U;
    const event_status_t status =
        event_subscribe(event_type, module_event_handler, (void*) (uintptr_t) module_id, &subscriber_id);

    if (status != EVENT_OK) {
        return event_status_to_module_error(status);
    }

    module_manager_lock();

    info = find_module_by_id_locked(module_id);
    if (info == NULL) {
        module_manager_unlock();
        (void) event_unsubscribe(event_type, subscriber_id);
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->event_subscription_count >= CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS ||
        find_event_sub_index(info, event_type) >= 0) {
        module_manager_unlock();
        (void) event_unsubscribe(event_type, subscriber_id);
        return MODULE_ERR_BUSY;
    }

    const uint8_t idx = info->event_subscription_count;

    info->event_subscriptions[idx].type = event_type;
    info->event_subscriptions[idx].subscriber_id = subscriber_id;
    info->event_subscription_count++;

    module_manager_unlock();
    return MODULE_OK;
}

int module_manager_unsubscribe(uint32_t module_id, event_type_t event_type) {
    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    const int idx = find_event_sub_index(info, event_type);

    if (idx < 0) {
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    const uint32_t sub_id = info->event_subscriptions[idx].subscriber_id;

    const uint8_t last = (uint8_t) (info->event_subscription_count - 1U);

    if ((uint8_t) idx != last) {
        info->event_subscriptions[idx] = info->event_subscriptions[last];
    }

    (void) memset(&info->event_subscriptions[last], 0, sizeof(info->event_subscriptions[last]));
    info->event_subscription_count = last;

    module_manager_unlock();

    (void) event_unsubscribe(event_type, sub_id);

    return MODULE_OK;
}

int module_manager_send_to_module(uint32_t module_id, const event_t* event) {
    if (event == NULL) {
        module_manager_lock();
        g_module_mgr.stats.events_dropped++;
        module_manager_unlock();
        return MODULE_ERR_INVALID_ARG;
    }

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL) {
        g_module_mgr.stats.events_dropped++;
        module_manager_unlock();
        return MODULE_ERR_NOT_FOUND;
    }

    if (info->status != MODULE_STATUS_RUNNING) {
        g_module_mgr.stats.events_dropped++;
        module_manager_unlock();
        return MODULE_ERR_NOT_RUNNING;
    }

    module_event_handler_t handler = info->interface->on_event;
    void*                  idata = info->internal_data;

    if (handler == NULL) {
        g_module_mgr.stats.events_dropped++;
        module_manager_unlock();
        return MODULE_ERR_INVALID_ARG;
    }

    g_module_mgr.stats.events_processed++;
    module_manager_unlock();

    handler(event, idata);
    return MODULE_OK;
}

int module_manager_broadcast(const event_t* event) {
    module_event_handler_t handlers[CONFIG_MAX_MODULES];
    void*                  datas[CONFIG_MAX_MODULES];
    int                    n = 0;

    if (event == NULL) {
        return MODULE_ERR_INVALID_ARG;
    }

    module_manager_lock();

    for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
        module_info_t* info = &g_module_mgr.modules[i];

        if (info->status == MODULE_STATUS_RUNNING && info->interface != NULL && info->interface->on_event != NULL) {
            handlers[n] = info->interface->on_event;
            datas[n] = info->internal_data;
            n++;
        }
    }

    g_module_mgr.stats.events_processed += (uint32_t) n;

    module_manager_unlock();

    for (int i = 0; i < n; i++) {
        handlers[i](event, datas[i]);
    }

    return n;
}

static void module_event_handler(const event_t* event, void* user_data) {
    if (event == NULL || user_data == NULL) {
        return;
    }

    const uint32_t module_id = (uint32_t) (uintptr_t) user_data;

    module_manager_lock();

    module_info_t* info = find_module_by_id_locked(module_id);

    if (info == NULL || info->interface == NULL || info->status != MODULE_STATUS_RUNNING) {
        module_manager_unlock();
        return;
    }

    module_event_handler_t handler = info->interface->on_event;
    void*                  idata = info->internal_data;

    g_module_mgr.stats.events_processed++;
    module_manager_unlock();

    if (handler != NULL) {
        handler(event, idata);
    }
}
