/**
 * @file remote_ops.h
 * @brief 远程运维钩子模块头文件
 *
 * 可选模块（CONFIG_REMOTE_OPS_MODULE）。Phase 5 通过可插拔后端导出
 * `sys_diag` JSON；mcumgr 真实传输留后续 target 叠加。事件 ID 71。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */

#ifndef REMOTE_OPS_H
#define REMOTE_OPS_H

#include <zeplod/event_system.h>
#include <zeplod/module_base.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_REMOTE_OPS_DIAG_EXPORTED ((event_type_t) 71)

typedef struct {
    uint32_t export_count;
    uint32_t last_payload_len;
} remote_ops_status_t;

int             remote_ops_init(void* config);
int             remote_ops_start(void);
int             remote_ops_stop(void);
int             remote_ops_shutdown(void);
void            remote_ops_on_event(const event_t* event, void* user_data);
module_status_t remote_ops_get_status(void);
int             remote_ops_control(int cmd, void* arg);

/**
 * @brief 采集诊断快照并以 JSON 经后端导出
 * @return 0 成功；APP_ERR_INIT（未 init 或未 RUNNING）；APP_ERR_REMOTE_OPS
 */
int remote_ops_export_diag(void);

/**
 * @brief 查询模块统计
 * @return 0 成功；APP_ERR_INIT；APP_ERR_INVALID_PARAM
 */
int remote_ops_get_stats(remote_ops_status_t* out);

/**
 * @brief 读回最近一次导出的 JSON（需后端实现 get_last_export）
 * @return 0 成功；APP_ERR_INIT；APP_ERR_REMOTE_OPS；APP_ERR_INVALID_PARAM；
 *         -ENOENT 尚无导出；-ENOMEM 缓冲不足
 */
int remote_ops_get_last_export(char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_OPS_H */
