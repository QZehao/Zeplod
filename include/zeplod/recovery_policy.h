/**
 * @file recovery_policy.h
 * @brief 恢复策略模块头文件
 *
 * 可选模块（CONFIG_RECOVERY_POLICY_MODULE）。监听 module_manager 的
 * MODULE_MGR_EVENT_ERROR，按 Kconfig 执行模块重启、全量重启或暖复位。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 2 初始版本
 *
 */

#ifndef RECOVERY_POLICY_H
#define RECOVERY_POLICY_H

#include <zeplod/event_system.h>
#include <zeplod/module_base.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 类型定义
 * ============================================================================= */

/**
 * @brief 恢复动作（与 Kconfig RECOVERY_POLICY_DEFAULT_ACTION 对应）
 */
typedef enum {
    RECOVERY_ACTION_NONE = 0,          /**< 仅记录日志 */
    RECOVERY_ACTION_RESTART_MODULE,    /**< stop + clear_error + start 失败模块 */
    RECOVERY_ACTION_RESTART_ALL,       /**< stop_all + start_all */
    RECOVERY_ACTION_REBOOT,            /**< sys_reboot(WARM) */
} recovery_action_t;

/** recovery_policy_control 命令：重置全部重启计数 */
#define RECOVERY_POLICY_CMD_RESET_COUNTS 1

/* =============================================================================
 * 模块接口（module_interface_t）
 * ============================================================================= */

int             recovery_policy_init(void* config);
int             recovery_policy_start(void);
int             recovery_policy_stop(void);
int             recovery_policy_shutdown(void);
void            recovery_policy_on_event(const event_t* event, void* user_data);
module_status_t recovery_policy_get_status(void);
int             recovery_policy_control(int cmd, void* arg);

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

/**
 * @brief 处理指定模块 ERROR（与 module_manager 回调路径相同）
 *
 * @param module_id 模块 ID
 * @return 0 已执行恢复动作；-EINVAL 参数或策略未运行
 */
int recovery_policy_on_module_error(uint32_t module_id);

/**
 * @brief 查询模块累计自动重启次数
 *
 * @param module_id 模块 ID
 * @return 次数；未知 ID 返回 0
 */
uint32_t recovery_policy_get_restart_count(uint32_t module_id);

/**
 * @brief 重置全部重启计数（测试用）
 */
void recovery_policy_reset_restart_counts(void);

#ifdef __cplusplus
}
#endif

#endif /* RECOVERY_POLICY_H */
