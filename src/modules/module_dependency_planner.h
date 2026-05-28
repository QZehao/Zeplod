/**
 * @file module_dependency_planner.h
 * @brief 模块批量启停依赖规划（拓扑 + 优先级）
 *
 * 调用方负责收集 start_order_entry_t 快照；拓扑排序路径会在函数内部短暂获取 module_manager 锁。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#ifndef MODULE_DEPENDENCY_PLANNER_H
#define MODULE_DEPENDENCY_PLANNER_H

#include "module_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
typedef struct {
    uint32_t           id;
    module_priority_t  priority;
    const char* const* depends_on;
} mm_dep_order_entry_t;
#else
typedef struct {
    uint32_t          id;
    module_priority_t priority;
} mm_dep_order_entry_t;
#endif

/** 与历史 start_order_entry_t 布局相同 */
typedef mm_dep_order_entry_t start_order_entry_t;

/** 按 priority 升序（数值小优先启动） */
void mm_dep_planner_sort_priority_asc(mm_dep_order_entry_t* entries, int n);

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)

/** 按 priority 降序（停止时作 fallback） */
void mm_dep_planner_sort_priority_desc(mm_dep_order_entry_t* entries, int n);

/**
 * @brief 校验批次内依赖并拓扑排序（启动顺序）
 * @pre 调用方不得持有 module_manager_lock
 * @return 有效条目数（可能小于 n）
 */
int mm_dep_planner_build_start_order(mm_dep_order_entry_t* entries, int n);

/**
 * @brief 拓扑排序后反转为停止顺序
 * @pre 调用方不得持有 module_manager_lock
 */
int mm_dep_planner_build_stop_order(mm_dep_order_entry_t* entries, int n);

#endif /* CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES */

#ifdef __cplusplus
}
#endif

#endif /* MODULE_DEPENDENCY_PLANNER_H */
