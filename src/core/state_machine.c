/**
 * @file state_machine.c
 * @brief 轻量状态机辅助实现
 *
 * 实现 zepl_state_t 生命周期状态的初始化、查询、转移校验与名称解析。
 *
 * 主要功能：
 * - 状态机初始化与当前状态读取
 * - 终态判断与合法转移表
 * - try_transition 原子语义的状态更新（由调用方保证并发安全）
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

#include "state_machine.h"

#include <errno.h>
#include <stddef.h>

void zepl_state_machine_init(zepl_state_machine_t* machine, zepl_state_t initial_state) {
    if (machine == NULL) {
        return;
    }

    machine->current = initial_state;
}

zepl_state_t zepl_state_machine_get(const zepl_state_machine_t* machine) {
    if (machine == NULL) {
        return ZEP_STATE_ERROR;
    }

    return machine->current;
}

bool zepl_state_is_terminal(zepl_state_t state) {
    return state == ZEP_STATE_STOPPED || state == ZEP_STATE_ERROR;
}

bool zepl_state_can_transition(zepl_state_t from, zepl_state_t to) {
    if (from == to) {
        return true;
    }

    switch (from) {
    case ZEP_STATE_UNINIT:
        return to == ZEP_STATE_INITED || to == ZEP_STATE_ERROR;
    case ZEP_STATE_INITED:
        return to == ZEP_STATE_STARTING || to == ZEP_STATE_STOPPING || to == ZEP_STATE_ERROR;
    case ZEP_STATE_STARTING:
        return to == ZEP_STATE_RUNNING || to == ZEP_STATE_STOPPING || to == ZEP_STATE_ERROR;
    case ZEP_STATE_RUNNING:
        return to == ZEP_STATE_STOPPING || to == ZEP_STATE_ERROR;
    case ZEP_STATE_STOPPING:
        return to == ZEP_STATE_STOPPED || to == ZEP_STATE_ERROR;
    case ZEP_STATE_STOPPED:
        return to == ZEP_STATE_INITED || to == ZEP_STATE_STARTING || to == ZEP_STATE_ERROR;
    case ZEP_STATE_ERROR:
        return to == ZEP_STATE_UNINIT || to == ZEP_STATE_INITED || to == ZEP_STATE_STOPPED;
    default:
        return false;
    }
}

int zepl_state_machine_try_transition(zepl_state_machine_t* machine, zepl_state_t next_state) {
    if (machine == NULL) {
        return -EINVAL;
    }

    if (!zepl_state_can_transition(machine->current, next_state)) {
        return -EPERM;
    }

    machine->current = next_state;
    return 0;
}

const char* zepl_state_name(zepl_state_t state) {
    switch (state) {
    case ZEP_STATE_UNINIT:
        return "UNINIT";
    case ZEP_STATE_INITED:
        return "INITED";
    case ZEP_STATE_STARTING:
        return "STARTING";
    case ZEP_STATE_RUNNING:
        return "RUNNING";
    case ZEP_STATE_STOPPING:
        return "STOPPING";
    case ZEP_STATE_STOPPED:
        return "STOPPED";
    case ZEP_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
