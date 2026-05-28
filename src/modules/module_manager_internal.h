/**
 * @file module_manager_internal.h
 * @brief 模块管理器内部共享（不对外公开）
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-05-28
 */

#ifndef MODULE_MANAGER_INTERNAL_H
#define MODULE_MANAGER_INTERNAL_H

#include "module_manager.h"
#include "module_dependency_planner.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "state_machine.h"

#define MM_MODULE_NAME_MAX 32

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
#define MM_ADJ_ROW_WORDS ((CONFIG_MAX_MODULES + 31) / 32)
#endif

typedef struct {
    module_info_t         modules[CONFIG_MAX_MODULES];
    uint32_t              module_count;
    module_mgr_stats_t    stats;
    module_mgr_callback_t callback;
    void*                 callback_user_data;
    struct k_mutex        lock;
    zepl_state_machine_t  lifecycle;
    bool                  initialized;
    bool                  running;
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
    uint32_t topo_adj_scratch[CONFIG_MAX_MODULES][MM_ADJ_ROW_WORDS];
#endif
} module_manager_cb_t;

extern module_manager_cb_t g_module_mgr;
extern atomic_t          g_module_mgr_shutting_down;
extern atomic_t          g_module_mgr_initialized;

void mm_copy_module_name(char* dst, const char* src);

void module_manager_lock(void);
void module_manager_unlock(void);
zepl_state_t module_manager_lifecycle_state_locked(void);

module_info_t* find_module_by_id_locked(uint32_t module_id);
uint32_t       find_module_id_by_name_locked(const char* name);
uint32_t       allocate_unique_module_id(void);
void           clear_module_slot_locked(module_info_t* info);

uint8_t module_event_detach_locked(module_info_t* info, event_type_t* types_out, uint32_t* ids_out, uint8_t max_out);
void    module_event_unsubscribe_batch(const event_type_t* types, const uint32_t* ids, uint8_t count);
void    module_mgr_notify_callback(uint32_t module_id, module_mgr_event_t evt);

int find_event_sub_index(const module_info_t* info, event_type_t type);
int event_status_to_module_error(event_status_t status);

#endif /* MODULE_MANAGER_INTERNAL_H */
