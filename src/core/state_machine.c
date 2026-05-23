/**
 * @file state_machine.c
 * @brief 通用状态机抽象实现（默认纯 C backend）
 *
 * 实现内置生命周期转移表及 zepl_state_machine_backend_t 默认虚表；
 * 不依赖 Zephyr SMF，后续可通过 zepl_state_machine_init_with_backend 替换实现。
 *
 * 主要功能：
 * - 默认状态转移规则（含 ERROR 恢复路径）
 * - 实例 API 经 backend 分发
 * - 无实例的 zepl_state_* 便捷函数
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

#include "state_machine.h"

#include <errno.h>
#include <stddef.h>

/* =============================================================================
 * 默认 backend：内部转移表与名称
 * ============================================================================= */

/**
 * @brief 默认转移规则（纯 C backend 核心逻辑）
 *
 * 允许同态（from == to）；ERROR 可回到 UNINIT / INITED / STOPPED 以便复位。
 */
static bool state_can_transition_default(zepl_state_t from, zepl_state_t to) {
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

/** @brief 默认终态：STOPPED 与 ERROR */
static bool state_is_terminal_default(zepl_state_t state) {
    return state == ZEP_STATE_STOPPED || state == ZEP_STATE_ERROR;
}

/** @brief 默认状态名称字符串表 */
static const char* state_name_default(zepl_state_t state) {
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

/**
 * @brief 默认 try_transition：校验后写入 machine->current
 */
static int state_try_transition_default(zepl_state_machine_t* machine, zepl_state_t next_state) {
    if (machine == NULL) {
        return -EINVAL;
    }

    if (!state_can_transition_default(machine->current, next_state)) {
        return -EPERM;
    }

    machine->current = next_state;
    return 0;
}

/* =============================================================================
 * 默认 backend 虚表适配层
 * ============================================================================= */

static bool default_backend_can_transition(const zepl_state_machine_t* machine, zepl_state_t from, zepl_state_t to) {
    (void) machine;
    return state_can_transition_default(from, to);
}

static int default_backend_try_transition(zepl_state_machine_t* machine, zepl_state_t next_state) {
    return state_try_transition_default(machine, next_state);
}

static const char* default_backend_state_name(const zepl_state_machine_t* machine, zepl_state_t state) {
    (void) machine;
    return state_name_default(state);
}

static bool default_backend_is_terminal(const zepl_state_machine_t* machine, zepl_state_t state) {
    (void) machine;
    return state_is_terminal_default(state);
}

/** 进程内单例默认 backend */
static const zepl_state_machine_backend_t g_default_backend = {
    .can_transition = default_backend_can_transition,
    .try_transition = default_backend_try_transition,
    .state_name = default_backend_state_name,
    .is_terminal = default_backend_is_terminal,
};

/* =============================================================================
 * backend 与初始化 API 实现
 * ============================================================================= */

const zepl_state_machine_backend_t* zepl_state_machine_default_backend(void) {
    return &g_default_backend;
}

void zepl_state_machine_init_with_backend(zepl_state_machine_t* machine, zepl_state_t initial_state,
                                          const zepl_state_machine_backend_t* backend, void* backend_context) {
    if (machine == NULL) {
        return;
    }

    machine->current = initial_state;
    machine->backend = backend != NULL ? backend : zepl_state_machine_default_backend();
    machine->backend_context = backend_context;
}

void zepl_state_machine_init(zepl_state_machine_t* machine, zepl_state_t initial_state) {
    zepl_state_machine_init_with_backend(machine, initial_state, zepl_state_machine_default_backend(), NULL);
}

int zepl_state_machine_set_backend(zepl_state_machine_t* machine, const zepl_state_machine_backend_t* backend,
                                   void* backend_context) {
    if (machine == NULL) {
        return -EINVAL;
    }

    machine->backend = backend != NULL ? backend : zepl_state_machine_default_backend();
    machine->backend_context = backend_context;
    return 0;
}

const zepl_state_machine_backend_t* zepl_state_machine_get_backend(const zepl_state_machine_t* machine) {
    if (machine == NULL || machine->backend == NULL) {
        return zepl_state_machine_default_backend();
    }

    return machine->backend;
}

void* zepl_state_machine_get_backend_context(const zepl_state_machine_t* machine) {
    if (machine == NULL) {
        return NULL;
    }

    return machine->backend_context;
}

/* =============================================================================
 * 状态查询与转移 API 实现
 * ============================================================================= */

zepl_state_t zepl_state_machine_get(const zepl_state_machine_t* machine) {
    if (machine == NULL) {
        return ZEP_STATE_ERROR;
    }

    return machine->current;
}

bool zepl_state_machine_can_transition(const zepl_state_machine_t* machine, zepl_state_t from, zepl_state_t to) {
    const zepl_state_machine_backend_t* backend = zepl_state_machine_get_backend(machine);

    if (backend != NULL && backend->can_transition != NULL) {
        return backend->can_transition(machine, from, to);
    }

    return state_can_transition_default(from, to);
}

int zepl_state_machine_try_transition(zepl_state_machine_t* machine, zepl_state_t next_state) {
    if (machine == NULL) {
        return -EINVAL;
    }

    const zepl_state_machine_backend_t* backend = zepl_state_machine_get_backend(machine);

    if (backend != NULL && backend->try_transition != NULL) {
        return backend->try_transition(machine, next_state);
    }

    return state_try_transition_default(machine, next_state);
}

const char* zepl_state_machine_get_name(const zepl_state_machine_t* machine, zepl_state_t state) {
    const zepl_state_machine_backend_t* backend = zepl_state_machine_get_backend(machine);

    if (backend != NULL && backend->state_name != NULL) {
        return backend->state_name(machine, state);
    }

    return state_name_default(state);
}

bool zepl_state_machine_is_terminal(const zepl_state_machine_t* machine, zepl_state_t state) {
    const zepl_state_machine_backend_t* backend = zepl_state_machine_get_backend(machine);

    if (backend != NULL && backend->is_terminal != NULL) {
        return backend->is_terminal(machine, state);
    }

    return state_is_terminal_default(state);
}

/* =============================================================================
 * 默认 backend 便捷 API 实现
 * ============================================================================= */

bool zepl_state_can_transition(zepl_state_t from, zepl_state_t to) {
    return state_can_transition_default(from, to);
}

const char* zepl_state_name(zepl_state_t state) {
    return state_name_default(state);
}

bool zepl_state_is_terminal(zepl_state_t state) {
    return state_is_terminal_default(state);
}
