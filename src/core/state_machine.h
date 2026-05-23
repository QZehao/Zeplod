/**
 * @file state_machine.h
 * @brief 通用状态机抽象头文件
 *
 * 提供与 Zephyr SMF 无关的统一生命周期状态枚举与转移 API；
 * 通过可插拔 backend 支持纯 C 默认实现或后续替换为其他状态机引擎。
 *
 * 主要特性：
 * - 标准子系统状态：UNINIT → INITED → STARTING → RUNNING → STOPPING → STOPPED / ERROR
 * - 合法转移校验与 try_transition
 * - 默认纯 C backend，可按实例绑定自定义 backend
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-05-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-23       1.1            zeh            增加可插拔 backend 抽象
 * 2026-05-23       1.0            zeh            初始版本
 *
 */

#ifndef ZEPLOD_CORE_STATE_MACHINE_H_
#define ZEPLOD_CORE_STATE_MACHINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 类型定义
 * ============================================================================= */

/**
 * @brief 通用生命周期状态枚举
 *
 * 供事件系统、模块管理器等子系统统一表达 init/start/stop/shutdown 阶段。
 */
typedef enum {
    ZEP_STATE_UNINIT = 0,  /**< 未初始化 */
    ZEP_STATE_INITED,      /**< 已初始化，尚未运行 */
    ZEP_STATE_STARTING,    /**< 正在启动 */
    ZEP_STATE_RUNNING,     /**< 正在运行 */
    ZEP_STATE_STOPPING,    /**< 正在停止 */
    ZEP_STATE_STOPPED,     /**< 已停止，可再次启动 */
    ZEP_STATE_ERROR,       /**< 错误态，需显式恢复或复位 */
} zepl_state_t;

/** 不透明前向声明；完整定义见下方 struct zepl_state_machine */
typedef struct zepl_state_machine zepl_state_machine_t;

/**
 * @brief 状态机 backend 虚表
 *
 * 各函数指针可为 NULL，状态机 API 在 NULL 时回退到内置默认实现。
 * backend_context 由调用方持有，在回调中通过 zepl_state_machine_get_backend_context 获取。
 */
typedef struct {
    /**
     * @brief 判断 from → to 是否允许
     * @param machine 状态机实例（可为只读）
     * @param from 当前状态
     * @param to 目标状态
     */
    bool (*can_transition)(const zepl_state_machine_t* machine, zepl_state_t from, zepl_state_t to);

    /**
     * @brief 尝试转移到 next_state
     * @param machine 状态机实例
     * @param next_state 目标状态
     * @return 0 成功，-EINVAL 参数无效，-EPERM 非法转移
     */
    int (*try_transition)(zepl_state_machine_t* machine, zepl_state_t next_state);

    /**
     * @brief 返回状态的日志/调试名称
     * @return 静态字符串，生命周期由 backend 管理
     */
    const char* (*state_name)(const zepl_state_machine_t* machine, zepl_state_t state);

    /**
     * @brief 判断是否为终态（默认 STOPPED / ERROR）
     */
    bool (*is_terminal)(const zepl_state_machine_t* machine, zepl_state_t state);
} zepl_state_machine_backend_t;

/**
 * @brief 状态机实例
 *
 * current 由 backend 的 try_transition 或默认实现更新；
 * 并发访问须由调用方用外部锁或原子语义保护。
 */
struct zepl_state_machine {
    zepl_state_t                        current;         /**< 当前状态 */
    const zepl_state_machine_backend_t* backend;         /**< 使用的 backend；NULL 时等价于默认 backend */
    void*                               backend_context; /**< 传给自定义 backend 的用户上下文 */
};

/* =============================================================================
 * 初始化与 backend 管理
 * ============================================================================= */

/**
 * @brief 使用默认 backend 初始化状态机
 *
 * @param machine 状态机实例指针
 * @param initial_state 初始状态（通常为 ZEP_STATE_UNINIT 或 ZEP_STATE_INITED）
 *
 * @note machine 为 NULL 时静默返回
 */
void zepl_state_machine_init(zepl_state_machine_t* machine, zepl_state_t initial_state);

/**
 * @brief 使用指定 backend 初始化状态机
 *
 * @param machine 状态机实例指针
 * @param initial_state 初始状态
 * @param backend backend 虚表；NULL 时使用 zepl_state_machine_default_backend()
 * @param backend_context 自定义 backend 上下文，可为 NULL
 */
void zepl_state_machine_init_with_backend(zepl_state_machine_t* machine, zepl_state_t initial_state,
                                          const zepl_state_machine_backend_t* backend, void* backend_context);

/**
 * @brief 运行时更换 backend（不改变 current）
 *
 * @param machine 状态机实例指针
 * @param backend 新 backend；NULL 时恢复默认 backend
 * @param backend_context 新上下文
 * @return 0 成功，-EINVAL machine 为 NULL
 */
int zepl_state_machine_set_backend(zepl_state_machine_t* machine, const zepl_state_machine_backend_t* backend,
                                   void* backend_context);

/**
 * @brief 获取进程内单例默认 backend（纯 C 实现）
 * @return 指向静态 backend 的指针，不可释放
 */
const zepl_state_machine_backend_t* zepl_state_machine_default_backend(void);

/**
 * @brief 获取实例当前绑定的 backend
 * @param machine 状态机实例；NULL 时返回默认 backend
 */
const zepl_state_machine_backend_t* zepl_state_machine_get_backend(const zepl_state_machine_t* machine);

/**
 * @brief 获取实例 backend 上下文
 * @param machine 状态机实例
 * @return backend_context；machine 为 NULL 时返回 NULL
 */
void* zepl_state_machine_get_backend_context(const zepl_state_machine_t* machine);

/* =============================================================================
 * 状态查询与转移（经实例 backend 分发）
 * ============================================================================= */

/**
 * @brief 读取当前状态
 * @param machine 状态机实例
 * @return 当前状态；machine 为 NULL 时返回 ZEP_STATE_ERROR
 */
zepl_state_t zepl_state_machine_get(const zepl_state_machine_t* machine);

/**
 * @brief 判断指定转移是否合法（不修改状态）
 * @param machine 状态机实例（用于选择 backend）
 * @param from 源状态
 * @param to 目标状态
 */
bool zepl_state_machine_can_transition(const zepl_state_machine_t* machine, zepl_state_t from, zepl_state_t to);

/**
 * @brief 尝试将状态机转移到 next_state
 *
 * @param machine 状态机实例
 * @param next_state 目标状态
 * @return 0 成功，-EINVAL 参数无效，-EPERM 非法转移
 *
 * @note 成功时更新 machine->current（默认 backend）或由自定义 backend 负责更新
 */
int zepl_state_machine_try_transition(zepl_state_machine_t* machine, zepl_state_t next_state);

/**
 * @brief 获取状态的调试名称
 * @param machine 状态机实例（选择 backend）
 * @param state 要查询的状态值
 * @return 静态字符串；未知状态返回 "UNKNOWN" 或 backend 自定义值
 */
const char* zepl_state_machine_get_name(const zepl_state_machine_t* machine, zepl_state_t state);

/**
 * @brief 判断状态是否为终态
 * @param machine 状态机实例（选择 backend）
 * @param state 要查询的状态值
 */
bool zepl_state_machine_is_terminal(const zepl_state_machine_t* machine, zepl_state_t state);

/* =============================================================================
 * 默认 backend 便捷 API（无实例，等价于默认转移表）
 * 适用于仅需校验/命名的静态场景
 * ============================================================================= */

/**
 * @brief 使用默认转移表判断 from → to 是否合法
 * @param from 源状态
 * @param to 目标状态
 */
bool zepl_state_can_transition(zepl_state_t from, zepl_state_t to);

/**
 * @brief 使用默认 backend 返回状态名称
 * @param state 状态枚举值
 */
const char* zepl_state_name(zepl_state_t state);

/**
 * @brief 使用默认 backend 判断是否为终态
 * @param state 状态枚举值
 */
bool zepl_state_is_terminal(zepl_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* ZEPLOD_CORE_STATE_MACHINE_H_ */
