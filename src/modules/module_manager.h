/**
 * @file module_manager.h
 * @brief 模块管理器头文件 (Module Manager Header)
 *
 * 提供模块的动态注册、生命周期管理和通信功能。
 *
 * 主要功能：
 * - 模块动态注册与注销
 * - 模块生命周期管理（初始化、启动、停止、关闭）
 * - 模块间事件通信
 * - 模块统计信息
 * - 模块状态回调
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULE_MANAGER_H
#define MODULE_MANAGER_H

#include "module_base.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置宏 (Configuration Macros)
 * ============================================================================= */

/**
 * @brief 最大支持的模块数量
 */
#ifndef CONFIG_MAX_MODULES
#define CONFIG_MAX_MODULES 16
#endif

/**
 * @brief 模块初始化超时时间（毫秒）
 * @note 如果模块 init() 函数执行时间超过此值，注册将失败
 * @note 设置为 0 时禁用超时检查
 */
#ifndef CONFIG_MODULE_INIT_TIMEOUT_MS
#define CONFIG_MODULE_INIT_TIMEOUT_MS 1000
#endif

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 模块管理器事件类型枚举
 * 
 * 用于通知模块状态变化。
 */
typedef enum {
    MODULE_MGR_EVENT_REGISTERED = 0,    /**< 模块已注册 */
    MODULE_MGR_EVENT_UNREGISTERED,      /**< 模块已注销 */
    MODULE_MGR_EVENT_STARTED,           /**< 模块已启动 */
    MODULE_MGR_EVENT_STOPPED,           /**< 模块已停止 */
    MODULE_MGR_EVENT_ERROR,             /**< 模块发生错误 */
    MODULE_MGR_EVENT_STATUS_CHANGED     /**< 模块状态已改变 */
} module_mgr_event_t;

/**
 * @brief 模块管理器回调函数类型
 * 
 * 当模块状态变化时调用此回调。
 * 
 * @param module_id 模块 ID
 * @param event 事件类型
 * @param user_data 用户数据
 */
typedef void (*module_mgr_callback_t)(uint32_t module_id,
                                       module_mgr_event_t event,
                                       void *user_data);

/**
 * @brief 模块管理器统计信息结构
 */
typedef struct {
    uint32_t total_modules;     /**< 总模块数量 */
    uint32_t active_modules;    /**< 活跃模块数量 */
    uint32_t error_modules;     /**< 错误模块数量 */
    uint32_t events_processed;  /**< 已处理的事件数量 */
    uint32_t events_dropped;    /**< 已丢弃的事件数量 */
} module_mgr_stats_t;

/* =============================================================================
 * 核心 API (Core API)
 * 模块管理器的初始化和关闭
 * ============================================================================= */

/**
 * @brief 初始化模块管理器
 * 
 * 初始化内部管理结构，必须在调用其他 API 之前调用。
 * 
 * @return 0 成功，负值错误码失败
 */
int module_manager_init(void);

/**
 * @brief 启动模块管理器
 * 
 * 启动管理器，允许模块注册和生命周期操作。
 * 
 * @return 0 成功，负值错误码失败
 */
int module_manager_start(void);

/**
 * @brief 停止模块管理器
 * 
 * 停止管理器，停止所有已注册的模块。
 * 
 * @return 0 成功，负值错误码失败
 */
int module_manager_stop(void);

/**
 * @brief 关闭模块管理器
 * 
 * 完全关闭管理器，注销所有模块并释放资源。
 * 
 * @return 0 成功，负值错误码失败
 */
int module_manager_shutdown(void);

/* =============================================================================
 * 模块注册 API (Module Registration API)
 * ============================================================================= */

/**
 * @brief 注册模块
 * 
 * 将模块注册到管理器中，调用模块的 init() 函数。
 * 
 * @param interface 模块接口指针
 * @param config 模块配置数据
 * @param module_id 输出参数：分配的模块 ID
 * @return 0 成功，负值错误码失败
 * 
 * @note init() 在管理器互斥锁持有时调用，不要在 init 中调用
 *       module_manager_* API（会导致死锁）
 * @note 如果 init 超过 CONFIG_MODULE_INIT_TIMEOUT_MS（当>0 时），
 *       注册将失败，如果 shutdown 非 NULL 则会被调用
 */
int module_manager_register(const module_interface_t *interface,
                            void *config,
                            uint32_t *module_id);

/**
 * @brief 注销模块
 * 
 * 从管理器中移除模块，调用模块的 stop() 和 shutdown() 函数。
 * 
 * @param module_id 模块 ID
 * @return 0 成功，负值错误码失败
 */
int module_manager_unregister(uint32_t module_id);

/**
 * @brief 获取模块信息（线程安全快照）
 * 
 * @param module_id 模块 ID
 * @param out 输出结构指针，不能为 NULL
 * @return 0 成功，-1 未找到或参数无效
 */
int module_manager_get_module_info(uint32_t module_id, module_info_t *out);

/**
 * @brief 按名称获取模块 ID
 * 
 * @param name 模块名称
 * @return 模块 ID，0 表示未找到
 */
uint32_t module_manager_get_id_by_name(const char *name);

/**
 * @brief 遍历所有模块
 * 
 * 对每个模块调用回调函数（在锁外执行，适合慢回调）。
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 * 
 * @note 不要在回调中调用 module_manager_* API（可能导致死锁）
 */
void module_manager_foreach(void (*callback)(module_info_t *, void *),
                            void *user_data);

/* =============================================================================
 * 模块生命周期 API (Module Lifecycle API)
 * ============================================================================= */

/**
 * @brief 启动指定模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，负值错误码失败
 */
int module_manager_start_module(uint32_t module_id);

/**
 * @brief 停止指定模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，负值错误码失败
 */
int module_manager_stop_module(uint32_t module_id);

/**
 * @brief 启动所有模块
 * 
 * 按优先级顺序启动所有已注册但未运行的模块。
 * 若启用 CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES，则先按 depends_on 拓扑排序，
 * 同层内仍按 priority；缺失依赖或成环时回退为仅按 priority。
 * 
 * @return 成功启动的模块数量
 */
int module_manager_start_all(void);

/**
 * @brief 停止所有模块
 * 
 * 停止所有正在运行的模块。
 * 若启用 CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES，则按依赖关系的逆序停止；
 * 否则保持内部槽位遍历顺序。
 * 
 * @return 成功停止的模块数量
 */
int module_manager_stop_all(void);

/**
 * @brief 挂起模块（临时禁用事件处理）
 * 
 * @param module_id 模块 ID
 * @return 0 成功，负值错误码失败
 */
int module_manager_suspend_module(uint32_t module_id);

/**
 * @brief 恢复被挂起的模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，负值错误码失败
 */
int module_manager_resume_module(uint32_t module_id);

/* =============================================================================
 * 事件处理 API (Event Handling API)
 * ============================================================================= */

/**
 * @brief 模块订阅事件类型
 * 
 * @param module_id 模块 ID
 * @param event_type 要订阅的事件类型
 * @return 0 成功，负值错误码失败
 */
int module_manager_subscribe(uint32_t module_id, event_type_t event_type);

/**
 * @brief 模块取消订阅事件类型
 * 
 * @param module_id 模块 ID
 * @param event_type 要取消订阅的事件类型
 * @return 0 成功，负值错误码失败
 */
int module_manager_unsubscribe(uint32_t module_id, event_type_t event_type);

/**
 * @brief 发送事件到指定模块
 * 
 * @param module_id 模块 ID
 * @param event 要发送的事件
 * @return 0 成功，负值错误码失败
 */
int module_manager_send_to_module(uint32_t module_id, const event_t *event);

/**
 * @brief 广播事件到所有模块
 * 
 * @param event 要广播的事件
 * @return 接收事件的模块数量
 */
int module_manager_broadcast(const event_t *event);

/* =============================================================================
 * 统计与调试 API (Statistics & Debug API)
 * ============================================================================= */

/**
 * @brief 获取模块管理器统计信息
 * 
 * @param stats 输出：统计信息结构指针
 */
void module_manager_get_stats(module_mgr_stats_t *stats);

/**
 * @brief 重置模块管理器统计信息
 */
void module_manager_reset_stats(void);

/**
 * @brief 打印模块信息到控制台
 */
void module_manager_dump_info(void);

/**
 * @brief 注册模块事件回调
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void module_manager_set_callback(module_mgr_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_H */
