/**
 * @file feature_gate.h
 * @brief 功能开关模块头文件（非安全 DRM）
 *
 * 可选模块（CONFIG_FEATURE_GATE_MODULE）。按 Kconfig 槽位 + 运行时 license
 * 字符串解锁高级能力；事件 ID 72。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */

#ifndef FEATURE_GATE_H
#define FEATURE_GATE_H

#include <zeplod/event_system.h>
#include <zeplod/module_base.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_FEATURE_GATE_LICENSE_CHANGED ((event_type_t) 72)

/** 功能名：始终可用 */
#define FEATURE_GATE_NAME_CORE "core"

/** 功能名：需有效 license（CONFIG_FEATURE_GATE_SLOT_CLOUD） */
#define FEATURE_GATE_NAME_CLOUD "cloud"

/** 功能名：需有效 license（CONFIG_FEATURE_GATE_SLOT_REMOTE） */
#define FEATURE_GATE_NAME_REMOTE "remote"

typedef struct {
    bool license_valid;
} feature_gate_status_t;

int             feature_gate_init(void* config);
int             feature_gate_start(void);
int             feature_gate_stop(void);
int             feature_gate_shutdown(void);
void            feature_gate_on_event(const event_t* event, void* user_data);
module_status_t feature_gate_get_status(void);
int             feature_gate_control(int cmd, void* arg);

/**
 * @brief 应用 license 令牌（与 Kconfig 期望值比对）
 *
 * 返回值 0 仅表示调用成功；token 是否有效请用 feature_gate_license_valid()。
 *
 * @return 0 成功；APP_ERR_INIT；APP_ERR_INVALID_PARAM
 */
int feature_gate_apply_license(const char* token);

/**
 * @brief 当前 license 是否有效
 */
bool feature_gate_license_valid(void);

/**
 * @brief 查询命名功能是否启用
 */
bool feature_gate_is_enabled(const char* feature_name);

/**
 * @brief 查询模块状态
 * @return 0 成功；APP_ERR_INIT；APP_ERR_INVALID_PARAM
 */
int feature_gate_get_status_snapshot(feature_gate_status_t* out);

#ifdef __cplusplus
}
#endif

#endif /* FEATURE_GATE_H */
