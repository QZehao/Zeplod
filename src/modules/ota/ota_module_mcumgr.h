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

/** 尝试独占 OTA ingest 会话（MCUmgr 与主动 API 互斥） */
bool ota_module_mcumgr_try_claim_session(void);
/** MCUmgr DFU 是否允许开始（模块 RUNNING 且无主动 ingest 占用） */
bool ota_module_mcumgr_is_accepting(void);
/** img_mgmt 收到 DFU 开始；返回 false 表示拒绝会话 */
bool ota_module_mcumgr_on_dfu_started(void);
/** SMP 分片写入进度回调 */
void ota_module_mcumgr_on_chunk_progress(size_t image_size);
/** 镜像写入完成，进入 pending 校验态 */
void ota_module_mcumgr_on_dfu_pending(void);
/** SMP 上传结束（成功或中止） */
void ota_module_mcumgr_on_dfu_stopped(void);
/** 取消进行中的 SMP 上传并释放 ingest 所有权 */
void ota_transport_mcumgr_smp_cancel_upload(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MODULE_MCUMGR_H */
