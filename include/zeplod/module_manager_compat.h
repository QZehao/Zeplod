/**
 * @file module_manager_compat.h
 * @brief 模块管理器兼容层 - 标准版统一入口
 *
 * 提供模块管理器的薄封装，使应用代码统一使用 module_compat_* API。
 * 商业版（PRO）模块已移出本框架，本头文件仅保留标准版映射。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 *
 */

#ifndef MODULE_MANAGER_COMPAT_H
#define MODULE_MANAGER_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#include <zeplod/module_base.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置与统计（统一格式）
 * ============================================================================= */

/**
 * @brief 模块管理器配置（传入 compat_init）
 */
typedef struct {
    uint16_t max_modules;      /**< @note 当前实现未使用：标准版槽位数由 CONFIG_MAX_MODULES 决定 */
    uint16_t max_dependencies; /**< 保留字段，标准版未使用 */
    bool     enable_auto_deps;
    bool     enable_hotplug;
    bool     enable_lifecycle_hooks;
    bool     enable_health_monitor;
} module_compat_config_t;

/**
 * @brief 模块管理器统计信息（统一格式）
 */
typedef struct {
    uint32_t total_modules;
    uint32_t active_modules;
    uint32_t error_modules;
    uint32_t events_processed;
    uint32_t events_dropped;
    uint32_t hotplug_events;
    uint32_t dependency_resolutions;
    uint32_t health_check_cycles;
} module_compat_stats_t;

/* =============================================================================
 * 统一初始化 / 统计接口
 * ============================================================================= */

int  module_compat_init(const module_compat_config_t* config);
int  module_compat_start(void);
int  module_compat_stop(void);
int  module_compat_shutdown(void);
void module_compat_get_stats(module_compat_stats_t* stats);
void module_compat_reset_stats(void);

/* =============================================================================
 * 模块 API：标准版宏映射
 * ============================================================================= */

#include <zeplod/module_manager.h>

#define module_compat_register(interface, config, module_id) module_manager_register((interface), (config), (module_id))
#define module_compat_unregister(module_id)                  module_manager_unregister((module_id))
#define module_compat_get_module_info(module_id, out)        module_manager_get_module_info((module_id), (out))
#define module_compat_get_id_by_name(name)                   module_manager_get_id_by_name((name))
#define module_compat_foreach(callback, user_data)           module_manager_foreach((callback), (user_data))
#define module_compat_start_module(module_id)                module_manager_start_module((module_id))
#define module_compat_stop_module(module_id)                 module_manager_stop_module((module_id))
#define module_compat_start_all()                            module_manager_start_all()
#define module_compat_stop_all()                             module_manager_stop_all()
#define module_compat_suspend_module(module_id)              module_manager_suspend_module((module_id))
#define module_compat_resume_module(module_id)               module_manager_resume_module((module_id))
#define module_compat_subscribe(module_id, event_type)       module_manager_subscribe((module_id), (event_type))
#define module_compat_unsubscribe(module_id, event_type)     module_manager_unsubscribe((module_id), (event_type))
#define module_compat_send_to_module(module_id, event)       module_manager_send_to_module((module_id), (event))
#define module_compat_broadcast(event)                       module_manager_broadcast((event))
#define module_compat_dump_info()                            module_manager_dump_info()
#define module_compat_set_callback(callback, user_data)      module_manager_set_callback((callback), (user_data))

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_COMPAT_H */
