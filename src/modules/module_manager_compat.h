/**
 * @file module_manager_compat.h
 * @brief 模块管理器兼容层 - 标准版与商业版统一接口
 *
 * 提供模块管理器的抽象层，使得应用代码可以在标准版和商业版之间无缝切换。
 *
 * 使用方式：
 * - 标准版：默认使用，无需额外配置
 * - 商业版：在 prj.conf 中设置 CONFIG_USE_MODULE_MANAGER_PRO=y
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-09
 */

#ifndef MODULE_MANAGER_COMPAT_H
#define MODULE_MANAGER_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置检测
 * ============================================================================= */

/**
 * @brief 检测是否使用商业版模块管理器
 * @note 用户只需在 prj.conf 中定义 CONFIG_USE_MODULE_MANAGER_PRO=y
 */
#if defined(CONFIG_USE_MODULE_MANAGER_PRO) && defined(CONFIG_MODULE_MANAGER_PRO)
#define MODULE_COMPAT_USE_PRO 1
#else
#define MODULE_COMPAT_USE_PRO 0
#endif

/* =============================================================================
 * 前向声明（避免重复定义）
 * ============================================================================= */

/* module_interface_t 和 module_info_t 已 在 module_base.h 中定义 */

/**
 * @brief 模块管理器配置（商业版兼容）
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
    /* 商业版扩展统计 */
    uint32_t hotplug_events;
    uint32_t dependency_resolutions;
    uint32_t health_check_cycles;
} module_compat_stats_t;

/* =============================================================================
 * 统一初始化接口
 * ============================================================================= */

/**
 * @brief 初始化模块管理器（统一入口）
 *
 * 根据配置自动选择标准版或商业版初始化。
 *
 * @param config 商业版配置（标准版忽略，传 NULL 即可）
 * @return 0 成功，负值错误码
 */
int module_compat_init(const module_compat_config_t* config);

/**
 * @brief 启动模块管理器（统一入口）
 * @return 0 成功，负值错误码
 */
int module_compat_start(void);

/**
 * @brief 停止模块管理器（统一入口）
 * @return 0 成功，负值错误码
 */
int module_compat_stop(void);

/**
 * @brief 关闭模块管理器（统一入口）
 * @return 0 成功，负值错误码
 */
int module_compat_shutdown(void);

/**
 * @brief 获取模块管理器统计信息（统一格式）
 *
 * 标准版：填充基础统计
 * 商业版：填充基础统计 + 扩展统计
 *
 * @param stats 输出统计信息
 */
void module_compat_get_stats(module_compat_stats_t* stats);

/**
 * @brief 重置统计信息
 */
void module_compat_reset_stats(void);

/* =============================================================================
 * 模块注册接口（直接映射到标准 API）
 * ============================================================================= */

/**
 * @brief 注册模块（标准版和商业版 API 兼容）
 * @note 两个版本 API 签名相同
 */
#define module_compat_register(interface, config, module_id) module_manager_register(interface, config, module_id)

/**
 * @brief 注销模块
 */
#define module_compat_unregister(module_id)                  module_manager_unregister(module_id)

/**
 * @brief 获取模块信息
 */
#define module_compat_get_module_info(module_id, out)        module_manager_get_module_info(module_id, out)

/**
 * @brief 按名称获取模块 ID
 */
#define module_compat_get_id_by_name(name)                   module_manager_get_id_by_name(name)

/**
 * @brief 遍历所有模块
 */
#define module_compat_foreach(callback, user_data)           module_manager_foreach(callback, user_data)

/**
 * @brief 启动指定模块
 */
#define module_compat_start_module(module_id)                module_manager_start_module(module_id)

/**
 * @brief 停止指定模块
 */
#define module_compat_stop_module(module_id)                 module_manager_stop_module(module_id)

/**
 * @brief 启动所有模块
 */
#define module_compat_start_all()                            module_manager_start_all()

/**
 * @brief 停止所有模块
 */
#define module_compat_stop_all()                             module_manager_stop_all()

/**
 * @brief 挂起模块
 */
#define module_compat_suspend_module(module_id)              module_manager_suspend_module(module_id)

/**
 * @brief 恢复模块
 */
#define module_compat_resume_module(module_id)               module_manager_resume_module(module_id)

/**
 * @brief 模块订阅事件
 */
#define module_compat_subscribe(module_id, event_type)       module_manager_subscribe(module_id, event_type)

/**
 * @brief 模块取消订阅
 */
#define module_compat_unsubscribe(module_id, event_type)     module_manager_unsubscribe(module_id, event_type)

/**
 * @brief 发送事件到指定模块
 */
#define module_compat_send_to_module(module_id, event)       module_manager_send_to_module(module_id, event)

/**
 * @brief 广播事件
 */
#define module_compat_broadcast(event)                       module_manager_broadcast(event)

/**
 * @brief 打印模块信息
 */
#define module_compat_dump_info()                            module_manager_dump_info()

/**
 * @brief 注册模块回调
 */
#define module_compat_set_callback(callback, user_data)      module_manager_set_callback(callback, user_data)

/* =============================================================================
 * 商业版 API 前置声明（用于编译检测）
 * ============================================================================= */

#if MODULE_COMPAT_USE_PRO
#include "module_manager_pro.h"
#else
#include "module_manager.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_COMPAT_H */
