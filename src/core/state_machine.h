/**
 * @file state_machine.h
 * @brief 轻量状态机辅助头文件
 *
 * 提供通用生命周期状态枚举与转移校验，供子系统（事件系统、模块等）统一状态语义。
 *
 * 主要特性：
 * - 标准状态：UNINIT → INITED → STARTING → RUNNING → STOPPING → STOPPED / ERROR
 * - 合法转移判断与 try_transition
 * - 终态与状态名称字符串
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

#ifndef ZEPLOD_CORE_STATE_MACHINE_H_
#define ZEPLOD_CORE_STATE_MACHINE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZEP_STATE_UNINIT = 0,
    ZEP_STATE_INITED,
    ZEP_STATE_STARTING,
    ZEP_STATE_RUNNING,
    ZEP_STATE_STOPPING,
    ZEP_STATE_STOPPED,
    ZEP_STATE_ERROR,
} zepl_state_t;

typedef struct {
    zepl_state_t current;
} zepl_state_machine_t;

void zepl_state_machine_init(zepl_state_machine_t* machine, zepl_state_t initial_state);
zepl_state_t zepl_state_machine_get(const zepl_state_machine_t* machine);
bool zepl_state_can_transition(zepl_state_t from, zepl_state_t to);
int zepl_state_machine_try_transition(zepl_state_machine_t* machine, zepl_state_t next_state);
const char* zepl_state_name(zepl_state_t state);
bool zepl_state_is_terminal(zepl_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* ZEPLOD_CORE_STATE_MACHINE_H_ */
