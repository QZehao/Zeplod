/**
 * @file connectivity_backend_null.c
 * @brief Null 连接后端（ztest / native_sim 同步模拟链路）
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

#include <zeplod/connectivity_backend.h>

#include <errno.h>
#include <stddef.h>

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

/** Null 后端上下文：内存中模拟链路 up/down */
typedef struct {
    bool link_up; /**< 当前是否视为已连接 */
    bool inited;  /**< init 是否已成功 */
} connectivity_null_ctx_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static connectivity_null_ctx_t    g_null_ctx;
static connectivity_backend_ops_t g_null_ops;

/* =============================================================================
 * 后端回调
 * ============================================================================= */

static int null_init(connectivity_backend_ops_t* ops) {
    connectivity_null_ctx_t* ctx = (connectivity_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return -EINVAL;
    }

    ctx->link_up = false;
    ctx->inited = true;
    return 0;
}

/** 同步将链路标记为 up（无真实网络 I/O） */
static int null_connect(connectivity_backend_ops_t* ops) {
    connectivity_null_ctx_t* ctx = (connectivity_null_ctx_t*) ops->ctx;

    if (ctx == NULL || !ctx->inited) {
        return -EINVAL;
    }

    ctx->link_up = true;
    return 0;
}

static int null_disconnect(connectivity_backend_ops_t* ops) {
    connectivity_null_ctx_t* ctx = (connectivity_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return -EINVAL;
    }

    ctx->link_up = false;
    return 0;
}

static bool null_is_link_up(const connectivity_backend_ops_t* ops) {
    const connectivity_null_ctx_t* ctx = (const connectivity_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return false;
    }
    return ctx->link_up;
}

/* =============================================================================
 * 公开 API
 * ============================================================================= */

/** 填充 ops 表并返回单例（每次调用刷新函数指针） */
const connectivity_backend_ops_t* connectivity_backend_null_get(void) {
    g_null_ops.ctx = &g_null_ctx;
    g_null_ops.init = null_init;
    g_null_ops.connect = null_connect;
    g_null_ops.disconnect = null_disconnect;
    g_null_ops.is_link_up = null_is_link_up;
    return &g_null_ops;
}
