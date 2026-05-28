/**
 * @file module_manager.c
 * @brief 模块管理器全局状态与锁
 *
 * 实现按职责拆分至：
 * - module_manager_lifecycle.c   管理器/模块生命周期
 * - module_manager_registry.c      注册表与查询
 * - module_manager_dependency.c  依赖规划器（mm_dep_planner_*）
 * - module_manager_event.c       事件订阅与分发
 * - module_manager_stats.c       统计与回调
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 */

#include "module_manager.h"
#include "module_manager_internal.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include "lock_order.h"

LOG_MODULE_REGISTER(module_manager, CONFIG_SYS_LOG_LEVEL);

module_manager_cb_t g_module_mgr;

atomic_t g_module_mgr_shutting_down = ATOMIC_INIT(0);
atomic_t g_module_mgr_initialized = ATOMIC_INIT(0);

BUILD_ASSERT(sizeof(module_info_t) * CONFIG_MAX_MODULES <= 2048,
             "module_manager snapshot too large for stack; reduce CONFIG_MAX_MODULES or "
             "CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS");

void mm_copy_module_name(char* dst, const char* src) {
    if (dst == NULL) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, MM_MODULE_NAME_MAX - 1U);
    dst[MM_MODULE_NAME_MAX - 1U] = '\0';
}

zepl_state_t module_manager_lifecycle_state_locked(void) {
    return zepl_state_machine_get(&g_module_mgr.lifecycle);
}

void module_manager_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_GLOBAL, (uintptr_t) &g_module_mgr.lock);
    k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
}

void module_manager_unlock(void) {
    k_mutex_unlock(&g_module_mgr.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_GLOBAL, (uintptr_t) &g_module_mgr.lock);
}
