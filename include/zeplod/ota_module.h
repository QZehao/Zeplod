/**
 * @file ota_module.h
 * @brief OTA 模块公开 API、状态机与事件类型
 *
 * 可选模块（CONFIG_OTA_MODULE）。产品默认经 MCUmgr SMP 被动接入；主动路径（须
 * CONFIG_OTA_TRANSPORT_ACTIVE）：
 * ota_module_begin_update() → ota_module_write_chunk() × N → ota_module_finish_update()。
 *
 * 配置要求：CONFIG_OTA_MODULE=y，且 CONFIG_EVENT_MAX_TYPES > 51（事件 ID 50–51）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本（Phase 1 null 传输）
 * 2026-06-13       1.1            zeh            统一注释风格；同步发布 PROGRESS 事件
 * 2026-06-13       1.2            zeh            主动传输；ota_module_request_reboot
 * 2026-06-13       1.3            zeh            MCUmgr SMP 默认；双 ingest 互斥
 *
 */

#ifndef OTA_MODULE_H
#define OTA_MODULE_H

#include <zeplod/event_system.h>
#include <zeplod/module_base.h>
#include <zeplod/ota_transport.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 事件类型
 * ============================================================================= */

/** OTA 状态变化（负载 ota_progress_t） */
#define EVENT_OTA_STATE_CHANGED ((event_type_t) 50)
/** OTA 进度通知（负载 ota_progress_t，与 STATE_CHANGED 同步发布） */
#define EVENT_OTA_PROGRESS      ((event_type_t) 51)

/* =============================================================================
 * 状态与进度
 * ============================================================================= */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY_REBOOT,
    OTA_STATE_ERROR,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint8_t     percent;
    int         error_code;
} ota_progress_t;

/* =============================================================================
 * 状态机（可单独单元测试）
 * ============================================================================= */

typedef struct ota_sm {
    ota_state_t state;
    int         last_error;
} ota_sm_t;

void        ota_sm_init(ota_sm_t* sm);
ota_state_t ota_sm_get_state(const ota_sm_t* sm);
int         ota_sm_on_download_start(ota_sm_t* sm);
int         ota_sm_on_download_complete(ota_sm_t* sm);
int         ota_sm_on_verify_ok(ota_sm_t* sm);
int         ota_sm_on_error(ota_sm_t* sm, int error_code);

/* =============================================================================
 * 模块接口（module_interface_t）
 * ============================================================================= */

int             ota_module_init(void* config);
int             ota_module_start(void);
int             ota_module_stop(void);
int             ota_module_shutdown(void);
void            ota_module_on_event(const event_t* event, void* user_data);
module_status_t ota_module_get_status(void);
int             ota_module_control(int cmd, void* arg);

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

/**
 * @brief 开始 OTA 下载会话
 * @return 0 成功；APP_ERR_OTA_INVALID_STATE / APP_ERR_OTA_TRANSPORT / APP_ERR_INIT
 * @note 若当前为 ERROR 或 READY_REBOOT，会自动重置会话后再开始
 */
int ota_module_begin_update(void);

/**
 * @brief 写入镜像数据块
 * @param offset 镜像内偏移（字节）
 * @param data   数据指针
 * @param len    数据长度
 * @return 0 成功；APP_ERR_INVALID_PARAM / APP_ERR_OTA_INVALID_STATE 或传输层 errno
 */
int ota_module_write_chunk(size_t offset, const uint8_t* data, size_t len);

/**
 * @brief 完成下载并校验镜像
 * @return 0 成功；负 errno 或 APP_ERR_OTA_INVALID_STATE
 */
int ota_module_finish_update(void);

/**
 * @brief 查询当前 OTA 业务状态
 * @param out_state 输出状态指针
 * @return 0 成功；APP_ERR_INVALID_PARAM
 */
int ota_module_get_state(ota_state_t* out_state);

/**
 * @brief 中止当前 OTA 会话并回到 IDLE
 * @return 0
 */
int ota_module_abort_update(void);

/**
 * @brief 在 READY_REBOOT 状态下请求暖重启以切换镜像
 * @return 0 成功（通常不返回）；APP_ERR_OTA_INVALID_STATE
 * @note MCUboot 传输在 finish 时已调用 boot_request_upgrade(TEST)
 */
int ota_module_request_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MODULE_H */
