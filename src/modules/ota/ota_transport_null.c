/**
 * @file ota_transport_null.c
 * @brief Null OTA 传输：RAM 缓冲模拟镜像写入（native_sim 测试）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 */

#include <zeplod/ota_transport.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    uint8_t buf[CONFIG_OTA_NULL_TRANSPORT_BUF_SIZE];
    size_t  write_len;
    bool    open;
} ota_null_ctx_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static ota_null_ctx_t      g_null_ctx;
static ota_transport_ops_t g_null_ops;

/* =============================================================================
 * 传输回调
 * ============================================================================= */

static int null_open(ota_transport_ops_t* ops) {
    ota_null_ctx_t* ctx = (ota_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return -EINVAL;
    }
    memset(ctx->buf, 0, sizeof(ctx->buf));
    ctx->write_len = 0U;
    ctx->open = true;
    return 0;
}

static int null_write_chunk(ota_transport_ops_t* ops, size_t offset, const uint8_t* data, size_t len) {
    ota_null_ctx_t* ctx = (ota_null_ctx_t*) ops->ctx;

    if (ctx == NULL || data == NULL || len == 0U) {
        return -EINVAL;
    }
    if (!ctx->open) {
        return -EIO;
    }
    if (len > sizeof(ctx->buf) || offset > sizeof(ctx->buf) - len) {
        return -ENOMEM;
    }
    memcpy(ctx->buf + offset, data, len);
    if (offset + len > ctx->write_len) {
        ctx->write_len = offset + len;
    }
    return 0;
}

static int null_verify(ota_transport_ops_t* ops) {
    ota_null_ctx_t* ctx = (ota_null_ctx_t*) ops->ctx;

    if (ctx == NULL || !ctx->open) {
        return -EINVAL;
    }
    if (ctx->write_len == 0U) {
        return -ENODATA;
    }
    return 0;
}

static int null_close(ota_transport_ops_t* ops) {
    ota_null_ctx_t* ctx = (ota_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return -EINVAL;
    }
    ctx->open = false;
    return 0;
}

static void null_abort(ota_transport_ops_t* ops) {
    ota_null_ctx_t* ctx = (ota_null_ctx_t*) ops->ctx;

    if (ctx == NULL) {
        return;
    }
    ctx->open = false;
    ctx->write_len = 0U;
}

/* =============================================================================
 * 公开 API
 * ============================================================================= */

const ota_transport_ops_t* ota_transport_null_get(void) {
    g_null_ops.ctx = &g_null_ctx;
    g_null_ops.open = null_open;
    g_null_ops.write_chunk = null_write_chunk;
    g_null_ops.verify = null_verify;
    g_null_ops.close = null_close;
    g_null_ops.abort = null_abort;
    return &g_null_ops;
}
