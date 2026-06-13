/**
 * @file ota_state_machine.c
 * @brief OTA 下载/校验状态机
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

#include <zeplod/ota_module.h>

#include <errno.h>

/* =============================================================================
 * 状态机实现
 * ============================================================================= */

void ota_sm_init(ota_sm_t* sm) {
    if (sm == NULL) {
        return;
    }
    sm->state = OTA_STATE_IDLE;
    sm->last_error = 0;
}

ota_state_t ota_sm_get_state(const ota_sm_t* sm) {
    if (sm == NULL) {
        return OTA_STATE_ERROR;
    }
    return sm->state;
}

int ota_sm_on_download_start(ota_sm_t* sm) {
    if (sm == NULL) {
        return -EINVAL;
    }
    if (sm->state != OTA_STATE_IDLE) {
        return -EINVAL;
    }
    sm->state = OTA_STATE_DOWNLOADING;
    sm->last_error = 0;
    return 0;
}

int ota_sm_on_download_complete(ota_sm_t* sm) {
    if (sm == NULL) {
        return -EINVAL;
    }
    if (sm->state != OTA_STATE_DOWNLOADING) {
        return -EINVAL;
    }
    sm->state = OTA_STATE_VERIFYING;
    return 0;
}

int ota_sm_on_verify_ok(ota_sm_t* sm) {
    if (sm == NULL) {
        return -EINVAL;
    }
    if (sm->state != OTA_STATE_VERIFYING) {
        return -EINVAL;
    }
    sm->state = OTA_STATE_READY_REBOOT;
    return 0;
}

int ota_sm_on_error(ota_sm_t* sm, int error_code) {
    if (sm == NULL) {
        return -EINVAL;
    }
    if (sm->state == OTA_STATE_IDLE || sm->state == OTA_STATE_READY_REBOOT) {
        return -EINVAL;
    }
    sm->state = OTA_STATE_ERROR;
    sm->last_error = error_code;
    return 0;
}
