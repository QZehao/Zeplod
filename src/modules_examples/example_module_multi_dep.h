/**
 * @file example_module_multi_dep.h
 * @brief 多依赖示例模块（演示 depends_on 数组）
 *
 * English: Example module whose interface lists two direct dependencies
 * (example_module_a, example_module_b). Enable CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES
 * and CONFIG_EXAMPLE_MODULE_MULTI_DEP; keep modules A/B registered so start_all can
 * order this module after them.
 *
 * 中文：本模块演示单个模块声明多个运行时依赖；需开启运行时依赖排序，并与 A/B 一同注册，
 * start_all 才会在拓扑序上把本模块排在 A、B 之后。
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

#ifndef EXAMPLE_MODULE_MULTI_DEP_H
#define EXAMPLE_MODULE_MULTI_DEP_H

#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

const module_interface_t* example_module_multi_dep_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_MULTI_DEP_H */
