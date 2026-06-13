/**
 * @file connectivity_backend.h
 * @brief 连接后端抽象（可插拔 vtable）
 *
 * Phase 3 提供 null 后端（ztest / native_sim）；Wi-Fi 等后端后续叠加。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 3 初始版本
 *
 */

#ifndef CONNECTIVITY_BACKEND_H
#define CONNECTIVITY_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 后端 vtable
 * ============================================================================= */

typedef struct connectivity_backend_ops connectivity_backend_ops_t;

struct connectivity_backend_ops {
    void* ctx;
    int (*init)(connectivity_backend_ops_t* ops);
    int (*connect)(connectivity_backend_ops_t* ops);
    int (*disconnect)(connectivity_backend_ops_t* ops);
    bool (*is_link_up)(const connectivity_backend_ops_t* ops);
};

/* =============================================================================
 * 内置后端
 * ============================================================================= */

const connectivity_backend_ops_t* connectivity_backend_null_get(void);

#ifdef __cplusplus
}
#endif

#endif /* CONNECTIVITY_BACKEND_H */
