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
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#ifndef MODULE_MANAGER_H
#define MODULE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 模块管理器公共 API
 *
 * @note 所有公开 API 必须在**线程上下文**调用；勿在 ISR 中调用（互斥锁非 ISR 安全）。
 */

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

/**
 * @note MODULE_MANAGER_DEPENDS_LIST_MAX / CONFIG_MODULE_MANAGER_DEPENDS_LIST_MAX（Kconfig）
 *
 * 中文：每个模块的 depends_on 数组里，最多有多少个「直接依赖」模块名（不含最后的 NULL）；
 *       管理器按此上限遍历，避免未 NULL 终止时死循环。不是系统内模块总数，也不是依赖深度。
 *
 * EN: Per-module cap on dependency name entries in depends_on[] (excluding NULL);
 *     iterate guard. Not MAX_MODULES (system-wide slot count), not A→B→C depth.
 */

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 模块管理器错误码枚举
 *
 * 所有模块管理器 API 均返回此枚举值，使用负 errno 风格。
 */
typedef enum {
    MODULE_OK = 0,                          /**< 操作成功 */
    MODULE_ERR_INVALID_ARG = -EINVAL,       /**< 无效参数 */
    MODULE_ERR_NO_MEM = -ENOMEM,            /**< 内存不足 */
    MODULE_ERR_ALREADY_EXISTS = -EALREADY,  /**< 模块已存在 */
    MODULE_ERR_NOT_FOUND = -ENOENT,         /**< 模块未找到 */
    MODULE_ERR_NOT_INITIALIZED = -ENOTCONN, /**< 管理器未初始化 */
    MODULE_ERR_ALREADY_RUNNING = -EALREADY, /**< 管理器已运行 */
    MODULE_ERR_NOT_RUNNING = -ENOTCONN,     /**< 模块未运行 */
    MODULE_ERR_TIMEOUT = -ETIMEDOUT,        /**< 操作超时 */
    MODULE_ERR_IO = -EIO,                   /**< I/O 错误 */
    MODULE_ERR_BUSY = -EBUSY                /**< 资源忙碌 */
} module_mgr_result_t;

/**
 * @brief 模块管理器事件类型枚举
 *
 * 用于通知模块状态变化。
 */
typedef enum {
    MODULE_MGR_EVENT_REGISTERED = 0, /**< 模块已注册 */
    MODULE_MGR_EVENT_UNREGISTERED,   /**< 模块已注销 */
    MODULE_MGR_EVENT_STARTED,        /**< 模块已启动 */
    MODULE_MGR_EVENT_STOPPED,        /**< 模块已停止 */
    MODULE_MGR_EVENT_ERROR,          /**< 模块发生错误 */
    MODULE_MGR_EVENT_STATUS_CHANGED  /**< 模块状态已改变 */
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
typedef void (*module_mgr_callback_t)(uint32_t module_id, module_mgr_event_t event, void* user_data);

/**
 * @brief 模块管理器统计信息结构
 */
typedef struct {
    uint32_t total_modules;    /**< 总模块数量 */
    uint32_t active_modules;   /**< 活跃模块数量 */
    uint32_t error_modules;    /**< 错误模块数量 */
    uint32_t events_processed; /**< 已处理的事件数量 */
    uint32_t events_dropped;   /**< 已丢弃的事件数量 */
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
 * @note init() 在管理器互斥锁外调用（槽位占位后解锁再执行），但仍请不要在 init 内调用
 *       可能阻塞过久或依赖其它模块尚未就绪的 module_manager_* API。
 * @note 如果 init 超过 CONFIG_MODULE_INIT_TIMEOUT_MS（当>0 时），
 *       注册将失败，如果 shutdown 非 NULL 则会被调用
 */
int module_manager_register(const module_interface_t* interface, void* config, uint32_t* module_id);

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
int module_manager_get_module_info(uint32_t module_id, module_info_t* out);

/**
 * @brief 按名称获取模块 ID
 *
 * @param name 模块名称
 * @return 模块 ID，0 表示未找到
 */
uint32_t module_manager_get_id_by_name(const char* name);

/**
 * @brief 遍历所有模块
 *
 * 对每个模块调用回调函数（在锁外执行，适合慢回调）。
 *
 * @param callback 回调函数
 * @param user_data 用户数据
 *
 * @note 回调在管理器锁外执行，可调用 module_manager_* API；
 *       但若回调自行持有其它锁，请注意锁顺序避免与业务代码产生锁反转。
 */
void module_manager_foreach(void (*callback)(module_info_t*, void*), void* user_data);

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
 * @return 0 表示成功，或**幂等**：模块当前非 RUNNING 时亦返回 0（未再次调用业务 stop 回调）；
 *         未找到或参数无效时返回 -1
 */
int module_manager_stop_module(uint32_t module_id);

/**
 * @brief 启动所有模块
 *
 * 按优先级顺序启动所有已注册但未运行的模块。
 * 若启用 CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES：先对 depends_on 做不动点校验
 * （非法依赖被剔除后，依赖方也会逐轮剔除），再拓扑排序，同层按 priority；
 * 成环或内部不一致时回退为仅按 priority。
 * 若启用 CONFIG_MODULE_MANAGER_START_ALL_ABORT_ON_FAILURE：任一模块 start 失败则不再
 * 启动后续模块（利于依赖语义）。
 *
 * @return 成功启动的模块数量
 */
int module_manager_start_all(void);

/**
 * @brief 停止所有模块
 *
 * 停止所有正在运行的模块。
 * 若启用 CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES：在当前 RUNNING 集合上按依赖
 * 求拓扑序后逆序停止；否则保持内部槽位遍历顺序。
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
int module_manager_send_to_module(uint32_t module_id, const event_t* event);

/**
 * @brief 广播事件到所有模块
 *
 * @param event 要广播的事件
 * @return 接收事件的模块数量
 */
int module_manager_broadcast(const event_t* event);

/* =============================================================================
 * 统计与调试 API (Statistics & Debug API)
 * ============================================================================= */

/**
 * @brief 获取模块管理器统计信息
 *
 * @param stats 输出：统计信息结构指针
 */
void module_manager_get_stats(module_mgr_stats_t* stats);

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
void module_manager_set_callback(module_mgr_callback_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_H */
