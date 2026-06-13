/**
 * @file factory_mode.h
 * @brief 工厂产测模式模块头文件
 *
 * 可选模块（CONFIG_FACTORY_MODE_MODULE）。产线进入/退出、GPIO 环回（stub）、
 * 校准数据写入与提交，发布事件 70。
 * 配置要求：CONFIG_EVENT_MAX_TYPES > 70。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 4 初始版本
 *
 */

#ifndef FACTORY_MODE_H
#define FACTORY_MODE_H

#include <zeplod/event_system.h>
#include <zeplod/module_base.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 事件类型
 * ============================================================================= */

/** 工厂模式状态变化（负载 factory_status_t） */
#define EVENT_FACTORY_STATE_CHANGED ((event_type_t) 70)

/* =============================================================================
 * 类型定义
 * ============================================================================= */

typedef enum {
    FACTORY_STATE_INACTIVE = 0,
    FACTORY_STATE_ACTIVE,
    FACTORY_STATE_PASSED,
    FACTORY_STATE_FAILED,
} factory_state_t;

typedef struct {
    factory_state_t state;
    int             error_code;
    bool            gpio_passed;
} factory_status_t;

/** factory_mode_control 命令：重置内部状态（测试用） */
#define FACTORY_MODE_CMD_RESET 1

/* =============================================================================
 * 模块接口
 * ============================================================================= */

int             factory_mode_init(void* config);
int             factory_mode_start(void);
int             factory_mode_stop(void);
int             factory_mode_shutdown(void);
void            factory_mode_on_event(const event_t* event, void* user_data);
module_status_t factory_mode_get_status(void);
int             factory_mode_control(int cmd, void* arg);

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

/**
 * @brief 进入产测模式（仅 INACTIVE → ACTIVE）
 * @return 0 成功；APP_ERR_FACTORY / APP_ERR_INIT
 */
int factory_mode_enter(void);

/**
 * @brief 退出产测模式（→ INACTIVE，清除临时校准表）
 * @return 0 成功
 */
int factory_mode_exit(void);

/**
 * @brief 查询工厂模式状态
 *
 * @param out 输出状态；`error_code` 在 FAILED 时为最近一次失败码，否则为 0
 */
int factory_mode_get_state(factory_status_t* out);

/**
 * @brief 运行 GPIO 环回自检（Phase 4 stub 同步成功）
 * @return 0 成功；APP_ERR_FACTORY 未在 ACTIVE
 */
int factory_mode_run_gpio_loopback(void);

/**
 * @brief 写入临时校准项（仅 ACTIVE）
 * @return 0 成功；-EINVAL / -ENOMEM / APP_ERR_FACTORY
 */
int factory_mode_set_calibration(const char* key, const char* value);

/**
 * @brief 读取临时校准项
 */
int factory_mode_get_calibration(const char* key, char* out, size_t out_len);

/**
 * @brief 提交校准到 app_kv（键前缀 factory.cal.）并标记 PASSED
 *
 * 须已完成 GPIO 环回且处于 ACTIVE。
 * @return 0 成功；APP_ERR_FACTORY / APP_ERR_DISABLED（无 app_kv）
 */
int factory_mode_finalize_pass(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_MODE_H */
