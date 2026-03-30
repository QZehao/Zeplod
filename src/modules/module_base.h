/**
 * @file module_base.h
 * @brief 模块基类接口头文件 (Base Module Interface Header)
 *
 * 所有业务模块的抽象接口定义。
 *
 * 主要功能：
 * - 定义模块的标准接口（虚函数表）
 * - 提供模块状态和优先级枚举
 * - 提供模块注册宏
 * - 支持模块订阅事件系统
 *
 * 模块生命周期：
 * UNINITIALIZED -> INITIALIZING -> INITIALIZED -> RUNNING -> STOPPED
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULE_BASE_H
#define MODULE_BASE_H

#include "event_system.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 模块状态枚举
 * 
 * 描述模块在其生命周期中的当前状态。
 */
typedef enum {
    MODULE_STATUS_UNINITIALIZED = 0,  /**< 未初始化状态 */
    MODULE_STATUS_INITIALIZING,       /**< 正在初始化 */
    MODULE_STATUS_INITIALIZED,        /**< 已初始化，等待启动 */
    MODULE_STATUS_RUNNING,            /**< 正在运行 */
    MODULE_STATUS_STOPPED,            /**< 已停止 */
    MODULE_STATUS_ERROR,              /**< 错误状态 */
    MODULE_STATUS_SUSPENDED           /**< 已挂起（暂停） */
} module_status_t;

/**
 * @brief 模块优先级枚举
 * 
 * 决定模块启动顺序，数值越小优先级越高。
 */
typedef enum {
    MODULE_PRIORITY_LOW      = 10,  /**< 低优先级 */
    MODULE_PRIORITY_NORMAL   = 5,   /**< 普通优先级 */
    MODULE_PRIORITY_HIGH     = 2,   /**< 高优先级 */
    MODULE_PRIORITY_CRITICAL = 0    /**< 临界优先级（最高） */
} module_priority_t;

/**
 * @brief 模块事件处理器函数类型
 * 
 * 模块通过此函数处理接收到的事件。
 * 
 * @param event 接收到的事件指针
 * @param user_data 模块特定的用户数据
 */
typedef void (*module_event_handler_t)(const event_t *event, void *user_data);

/**
 * @brief 模块接口结构（虚函数表）
 * 
 * 定义模块必须实现的标准接口函数。
 * 所有业务模块都应实现此接口结构。
 */
typedef struct {
    const char *name;                   /**< 模块名称 */
    uint32_t version;                   /**< 模块版本号（MAJOR.MINOR.PATCH 编码） */
    module_priority_t priority;         /**< 模块优先级 */
    /**
     * 运行时依赖：指向以 NULL 结尾的字符串数组，每项为其它模块的 interface->name。
     * 仅在 Kconfig CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES 启用时参与排序；
     * 未启用或未使用时可置 NULL。
     */
    const char *const *depends_on;
    int (*init)(void *config);          /**< 初始化函数指针 */
    int (*start)(void);                 /**< 启动函数指针 */
    int (*stop)(void);                  /**< 停止函数指针 */
    int (*shutdown)(void);              /**< 关闭/销毁函数指针 */
    module_event_handler_t on_event;    /**< 事件处理函数指针 */
    module_status_t (*get_status)(void);/**< 获取状态函数指针 */
    int (*control)(int cmd, void *arg); /**< 控制命令函数指针 */
} module_interface_t;

/**
 * @brief 模块最大事件订阅数量
 * @note 每个模块最多可订阅的事件类型数量
 */
#ifndef CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS
#define CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS 8
#endif

/**
 * @brief 模块事件订阅结构
 * 
 * 表示模块对某个事件类型的订阅。
 */
typedef struct {
    event_type_t type;          /**< 事件类型 ID */
    uint32_t subscriber_id;     /**< 订阅者 ID（由事件系统分配） */
} module_event_subscription_t;

/**
 * @brief 模块注册信息结构
 * 
 * 包含模块注册后的所有管理信息。
 */
typedef struct {
    const module_interface_t *interface;            /**< 模块接口指针 */
    void *config;                                   /**< 模块配置数据 */
    void *internal_data;                            /**< 模块内部数据 */
    module_status_t status;                         /**< 当前模块状态 */
    uint32_t id;                                    /**< 模块唯一 ID */
    module_event_subscription_t event_subscriptions[CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS];
    uint8_t event_subscription_count;               /**< 已订阅的事件类型数量 */
} module_info_t;

/* =============================================================================
 * 模块辅助宏 (Module Helper Macros)
 * ============================================================================= */

/**
 * @brief 版本号编码宏
 * 
 * 将主版本号、次版本号、补丁号编码为 32 位整数。
 * 
 * @param major 主版本号
 * @param minor 次版本号
 * @param patch 补丁号
 * @return 编码后的版本号
 */
#define MODULE_VERSION(major, minor, patch) \
    (((major) << 16) | ((minor) << 8) | (patch))

/**
 * @brief 提取主版本号
 * @param v 编码后的版本号
 * @return 主版本号
 */
#define MODULE_VERSION_MAJOR(v)  (((v) >> 16) & 0xFF)

/**
 * @brief 提取次版本号
 * @param v 编码后的版本号
 * @return 次版本号
 */
#define MODULE_VERSION_MINOR(v)  (((v) >> 8) & 0xFF)

/**
 * @brief 提取补丁号
 * @param v 编码后的版本号
 * @return 补丁号
 */
#define MODULE_VERSION_PATCH(v)  ((v) & 0xFF)

/**
 * @brief 声明完整的模块接口（包含所有可选函数）
 * 
 * 此宏用于声明并定义一个完整的模块接口结构。
 * 要求模块实现所有接口函数：init, start, stop, shutdown, on_event, get_status, control
 * 
 * @param name 模块名称（也是函数前缀）
 * 
 * @note 使用示例：在模块 .c 文件中使用 DECLARE_MODULE_INTERFACE(my_module);
 */
#define DECLARE_MODULE_INTERFACE(name) \
    extern const module_interface_t name##_interface; \
    const module_interface_t name##_interface = { \
        .name = #name, \
        .version = MODULE_VERSION(1, 0, 0), \
        .priority = MODULE_PRIORITY_NORMAL, \
        .depends_on = NULL, \
        .init = name##_init, \
        .start = name##_start, \
        .stop = name##_stop, \
        .shutdown = name##_shutdown, \
        .on_event = name##_on_event, \
        .get_status = name##_get_status, \
        .control = name##_control \
    }

/**
 * @brief 与 DECLARE_MODULE_INTERFACE 相同，但显式指定 depends_on 数组（NULL 结尾）
 */
#define DECLARE_MODULE_INTERFACE_WITH_DEPS(name, deps_array) \
    extern const module_interface_t name##_interface; \
    const module_interface_t name##_interface = { \
        .name = #name, \
        .version = MODULE_VERSION(1, 0, 0), \
        .priority = MODULE_PRIORITY_NORMAL, \
        .depends_on = (deps_array), \
        .init = name##_init, \
        .start = name##_start, \
        .stop = name##_stop, \
        .shutdown = name##_shutdown, \
        .on_event = name##_on_event, \
        .get_status = name##_get_status, \
        .control = name##_control \
    }

/**
 * @brief 声明最小化的模块接口（可选函数为 NULL）
 * 
 * 此宏用于声明并定义一个最小化的模块接口结构。
 * 只要求模块实现必需函数：init, start, stop, on_event
 * shutdown, get_status, control 将被设置为 NULL
 * 
 * @param name 模块名称（也是函数前缀）
 * 
 * @note 管理器在调用这些函数前会进行 NULL 检查
 */
#define DECLARE_MODULE_INTERFACE_MINIMAL(name) \
    extern const module_interface_t name##_interface; \
    const module_interface_t name##_interface = { \
        .name = #name, \
        .version = MODULE_VERSION(1, 0, 0), \
        .priority = MODULE_PRIORITY_NORMAL, \
        .depends_on = NULL, \
        .init = name##_init, \
        .start = name##_start, \
        .stop = name##_stop, \
        .shutdown = NULL, \
        .on_event = name##_on_event, \
        .get_status = NULL, \
        .control = NULL \
    }

#define DECLARE_MODULE_INTERFACE_MINIMAL_WITH_DEPS(name, deps_array) \
    extern const module_interface_t name##_interface; \
    const module_interface_t name##_interface = { \
        .name = #name, \
        .version = MODULE_VERSION(1, 0, 0), \
        .priority = MODULE_PRIORITY_NORMAL, \
        .depends_on = (deps_array), \
        .init = name##_init, \
        .start = name##_start, \
        .stop = name##_stop, \
        .shutdown = NULL, \
        .on_event = name##_on_event, \
        .get_status = NULL, \
        .control = NULL \
    }

#ifdef __cplusplus
}
#endif

#endif /* MODULE_BASE_H */
