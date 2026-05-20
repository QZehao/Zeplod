/**
 * @file module_manager_compat.h
 * @brief 模块管理器兼容层 - 标准版与商业版统一入口
 *
 * 提供模块管理器的抽象层，使得应用代码可以在标准版和商业版之间切换。
 *
 * 使用方式：
 * - 标准版：默认使用，无需额外配置
 * - 商业版：在 prj.conf 中同时启用 CONFIG_USE_MODULE_MANAGER_PRO 与 CONFIG_MODULE_MANAGER_PRO
 *
 *       module_interface_t 二进制不兼容；注册/注销请使用 src/proprietary 头文件中的
 *       module_manager_pro_* API。本头文件中仅 init/start/stop/shutdown 与统计映射到 PRO。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-04-09
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

#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置检测
 * ============================================================================= */

#if defined(CONFIG_USE_MODULE_MANAGER_PRO) && defined(CONFIG_MODULE_MANAGER_PRO)
#define MODULE_COMPAT_USE_PRO 1
#else
#define MODULE_COMPAT_USE_PRO 0
#endif

/* =============================================================================
 * 配置与统计（统一格式）
 * ============================================================================= */

/**
 * @brief 模块管理器配置（传入 compat_init；映射到 PRO 时字段按语义对齐，见实现注释）
 */
typedef struct {
    uint16_t max_modules;
    uint16_t max_dependencies;
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
 * 模块 API：标准版为宏映射；PRO 版为函数（部分返回 -ENOTSUP，见各函数注释）
 * ============================================================================= */

#if !MODULE_COMPAT_USE_PRO

#include "module_manager.h"

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

#else /* MODULE_COMPAT_USE_PRO */

#include "module_manager.h"
#include "module_manager_pro.h"

int      module_compat_register(const module_interface_t* interface, void* config, uint32_t* module_id);
int      module_compat_unregister(uint32_t module_id);
int      module_compat_get_module_info(uint32_t module_id, module_info_t* out);
int      module_compat_foreach(void (*callback)(module_info_t*, void*), void* user_data);
int      module_compat_start_module(uint32_t module_id);
int      module_compat_stop_module(uint32_t module_id);
int      module_compat_start_all(void);
int      module_compat_stop_all(void);
int      module_compat_suspend_module(uint32_t module_id);
int      module_compat_resume_module(uint32_t module_id);
int      module_compat_subscribe(uint32_t module_id, event_type_t event_type);
int      module_compat_unsubscribe(uint32_t module_id, event_type_t event_type);
int      module_compat_send_to_module(uint32_t module_id, const event_t* event);
int      module_compat_broadcast(const event_t* event);
void     module_compat_dump_info(void);
int      module_compat_set_callback(module_mgr_callback_t callback, void* user_data);
uint32_t module_compat_get_id_by_name(const char* name);

#endif /* MODULE_COMPAT_USE_PRO */

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_COMPAT_H */
