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

typedef enum {
    ZEP_LOCK_LEVEL_GLOBAL = 1,
    ZEP_LOCK_LEVEL_STATE = 2,
    ZEP_LOCK_LEVEL_TABLE = 3,
    ZEP_LOCK_LEVEL_ENTRY = 4,
    ZEP_LOCK_LEVEL_RESOURCE = 5,
} zepl_lock_level_t;

typedef struct {
    zepl_lock_level_t level;
    uintptr_t         key;
} zepl_lock_token_t;

bool zepl_lock_order_is_valid(zepl_lock_level_t level, uintptr_t key);
void zepl_lock_enter(zepl_lock_level_t level, uintptr_t key);
void zepl_lock_exit(zepl_lock_level_t level, uintptr_t key);

void zepl_lock_enter_token(zepl_lock_token_t token);
void zepl_lock_exit_token(zepl_lock_token_t token);

void zepl_lock_reset_current_thread(void);
zepl_lock_token_t zepl_lock_current_token(void);
zepl_lock_level_t zepl_lock_current_level(void);
uintptr_t zepl_lock_current_key(void);
uint8_t zepl_lock_current_depth(void);

#define ZEPL_LOCK_TOKEN(level, lock_ptr) ((zepl_lock_token_t) { .level = (level), .key = (uintptr_t) (lock_ptr) })

#ifdef __cplusplus
}
#endif

#endif /* ZEPLOD_CORE_LOCK_ORDER_H_ */
