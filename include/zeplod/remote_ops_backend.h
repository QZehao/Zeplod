/**
 * @file remote_ops_backend.h
 * @brief 远程运维后端抽象
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */

#ifndef REMOTE_OPS_BACKEND_H
#define REMOTE_OPS_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct remote_ops_backend_ops remote_ops_backend_ops_t;

struct remote_ops_backend_ops {
    void* ctx;
    int (*init)(remote_ops_backend_ops_t* ops);
    int (*deinit)(remote_ops_backend_ops_t* ops);
    int (*export_diag)(remote_ops_backend_ops_t* ops, const char* json, size_t json_len);
    /**
     * @brief 读回最近一次 export_diag 载荷（可选；mcumgr 等无 RAM 缓存后端可置 NULL）
     * @return 0 成功；-ENOENT 尚无导出；-EINVAL / -ENOMEM
     */
    int (*get_last_export)(remote_ops_backend_ops_t* ops, char* out, size_t out_len);
};

const remote_ops_backend_ops_t* remote_ops_backend_null_get(void);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_OPS_BACKEND_H */
