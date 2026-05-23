/**
 * @file lock_order.h
 * @brief 锁顺序校验辅助头文件
 *
 * 按线程跟踪当前持有的锁层级与嵌套栈，用于调试期与轻量运行时的加锁顺序检查，
 * 降低多锁场景下的 AB-BA 死锁风险。
 *
 * 主要特性：
 * - 分级锁模型（GLOBAL / STATE / TABLE / ENTRY / RESOURCE）
 * - 每线程锁栈与 token 进出接口
 * - 可选的运行时顺序校验（zepl_lock_order_is_valid）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-23       1.0            zeh            初始版本
 *
 */

#ifndef ZEPLOD_CORE_LOCK_ORDER_H_
#define ZEPLOD_CORE_LOCK_ORDER_H_

#include <zephyr/kernel.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 配置宏
 * 可在编译单元中于包含本头文件之前覆盖默认值
 * ============================================================================= */

/**
 * @brief 锁顺序注册表可容纳的最大线程数
 * @note 超出后 zepl_lock_enter 将记录错误并忽略本次入栈
 */
#ifndef ZEP_LOCK_ORDER_MAX_THREADS
#define ZEP_LOCK_ORDER_MAX_THREADS 32U
#endif

/**
 * @brief 单线程锁嵌套栈最大深度
 * @note 超出后 zepl_lock_enter 将记录错误并忽略本次入栈
 */
#ifndef ZEP_LOCK_ORDER_MAX_DEPTH
#define ZEP_LOCK_ORDER_MAX_DEPTH 16U
#endif

/* =============================================================================
 * 类型定义
 * ============================================================================= */

/**
 * @brief 锁层级枚举（数值越小表示越外层、应先获取）
 *
 * 约定：后入栈的锁层级必须严格大于栈顶，或同层级 key 单调不减，
 * 以便与事件系统等模块的全局锁 → 表锁 → 条目锁顺序一致。
 */
typedef enum {
    ZEP_LOCK_LEVEL_GLOBAL = 1,   /**< 全局/子系统级（如 event_system.stats_lock） */
    ZEP_LOCK_LEVEL_STATE = 2,    /**< 状态机或生命周期锁 */
    ZEP_LOCK_LEVEL_TABLE = 3,    /**< 表/索引级（如 event_types 注册表） */
    ZEP_LOCK_LEVEL_ENTRY = 4,    /**< 单条目级（如 event_type_entry.lock） */
    ZEP_LOCK_LEVEL_RESOURCE = 5, /**< 细粒度资源锁 */
} zepl_lock_level_t;

/**
 * @brief 锁标识 token（层级 + 锁对象地址）
 *
 * key 通常取互斥锁或自旋锁对象的指针，用于区分同层级多把锁。
 */
typedef struct {
    zepl_lock_level_t level; /**< 锁层级 */
    uintptr_t         key;   /**< 锁实例唯一键（一般为锁指针） */
} zepl_lock_token_t;

/* =============================================================================
 * 锁顺序 API
 * 须在**线程上下文**调用；ISR 中 enter/exit 将被拒绝并记录警告
 * ============================================================================= */

/**
 * @brief 检查在当前线程栈顶之上再获取指定锁是否满足顺序规则
 *
 * 不修改锁栈，仅做只读校验，适用于在真正加锁前的探测。
 *
 * @param level 拟获取的锁层级
 * @param key 拟获取的锁键（通常为锁指针）
 * @return true 顺序合法，false 非法参数、ISR 上下文或违反顺序
 *
 * @note 若当前线程尚未登记任何锁，空栈视为合法
 */
bool zepl_lock_order_is_valid(zepl_lock_level_t level, uintptr_t key);

/**
 * @brief 记录一次加锁（入栈）
 *
 * 在调用方实际获取互斥锁/自旋锁之后调用；若检测到顺序违规或栈溢出，
 * 记录 LOG_ERR 且**不**入栈（调用方仍已持锁，需自行保证一致性）。
 *
 * @param level 已获取的锁层级
 * @param key 已获取的锁键
 *
 * @note 违规时仅记录日志，不触发断言，避免在调试辅助中扩大故障面
 */
void zepl_lock_enter(zepl_lock_level_t level, uintptr_t key);

/**
 * @brief 记录一次解锁（出栈）
 *
 * 必须与最近一次 zepl_lock_enter 的 level/key 完全匹配（LIFO）。
 * 栈空时出栈视为违规。
 *
 * @param level 即将释放的锁层级
 * @param key 即将释放的锁键
 *
 * @note 应在调用方释放互斥锁/自旋锁之前或之后紧邻调用，与 enter 成对
 */
void zepl_lock_exit(zepl_lock_level_t level, uintptr_t key);

/**
 * @brief 使用 token 入栈（zepl_lock_enter 的便捷封装）
 * @param token 由 ZEPL_LOCK_TOKEN 构造的锁标识
 */
void zepl_lock_enter_token(zepl_lock_token_t token);

/**
 * @brief 使用 token 出栈（zepl_lock_exit 的便捷封装）
 * @param token 须与对应 enter_token 一致
 */
void zepl_lock_exit_token(zepl_lock_token_t token);

/**
 * @brief 清空当前线程的锁栈登记
 *
 * 用于错误恢复、测试收尾或线程即将退出且无法按序释放的场景。
 *
 * @note 不会释放任何真实内核锁，仅重置本模块的跟踪状态
 */
void zepl_lock_reset_current_thread(void);

/**
 * @brief 获取当前线程栈顶锁 token
 * @return 栈顶 token；空栈或 ISR 时 level/key 均为 0
 */
zepl_lock_token_t zepl_lock_current_token(void);

/**
 * @brief 获取当前线程栈顶锁层级
 * @return 栈顶层级；空栈或 ISR 时返回 0
 */
zepl_lock_level_t zepl_lock_current_level(void);

/**
 * @brief 获取当前线程栈顶锁键
 * @return 栈顶 key；空栈或 ISR 时返回 0
 */
uintptr_t zepl_lock_current_key(void);

/**
 * @brief 获取当前线程已登记的锁嵌套深度
 * @return 栈深度（0 表示未登记任何锁）
 */
uint8_t zepl_lock_current_depth(void);

/* =============================================================================
 * 辅助宏
 * ============================================================================= */

/**
 * @brief 由层级与锁指针构造 zepl_lock_token_t
 * @param level zepl_lock_level_t 枚举值
 * @param lock_ptr 互斥锁或自旋锁对象指针
 */
#define ZEPL_LOCK_TOKEN(level, lock_ptr) ((zepl_lock_token_t) { .level = (level), .key = (uintptr_t) (lock_ptr) })

#ifdef __cplusplus
}
#endif

#endif /* ZEPLOD_CORE_LOCK_ORDER_H_ */
