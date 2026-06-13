/**
 * @file ota_module_mcumgr.h
 * @brief OTA 模块与 MCUmgr img_mgmt 回调桥接（内部头文件）
 *
 * ota_module.c 与 ota_transport_mcumgr_smp.c 共享。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 4 初始版本
 * 2026-06-13       1.1            zeh            会话互斥；与主动 ingest 并存
 * 2026-06-13       1.2            zeh            DFU_STARTED 返回值；取消上传 API
 *
 */

#ifndef OTA_MODULE_MCUMGR_H
#define OTA_MODULE_MCUMGR_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ota_module_mcumgr_try_claim_session(void);
bool ota_module_mcumgr_is_accepting(void);
bool ota_module_mcumgr_on_dfu_started(void);
void ota_module_mcumgr_on_chunk_progress(size_t image_size);
void ota_module_mcumgr_on_dfu_pending(void);
void ota_module_mcumgr_on_dfu_stopped(void);
void ota_transport_mcumgr_smp_cancel_upload(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MODULE_MCUMGR_H */
