/**
 * @file app_main.h
 * @brief 应用入口 API 与配置类型（版本信息见 app_version.h）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "app_config.h"
#include "app_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Application Configuration
 * ============================================================================= */

typedef struct {
    bool     enable_logging;
    bool     enable_watchdog;
    bool     enable_shell;
    uint32_t log_level;
} app_config_t;

/* =============================================================================
 * Application API
 * ============================================================================= */

/**
 * @brief 查询应用是否已由 SYS_INIT 完成初始化（在 main 之前执行）
 * @param config 保留；运行时传入的配置当前不参与 SYS_INIT 路径
 * @return APP_OK 若已初始化，否则 APP_ERR_INIT
 */
int app_init(const app_config_t* config);

/**
 * @brief 启动应用
 * @return APP_OK 成功，否则为 APP_ERR_*（见 app_config.h）
 */
int app_start(void);

/**
 * @brief 停止应用
 * @return APP_OK 成功，否则为 APP_ERR_*
 */
int app_stop(void);

/**
 * @brief Get application uptime
 * @return Uptime in milliseconds
 */
uint32_t app_get_uptime(void);

/**
 * @brief Check if application is running
 * @return true if running, false otherwise
 */
bool app_is_running(void);

/**
 * @brief Get application heartbeat count
 * @return Heartbeat count
 */
uint32_t app_get_heartbeat_count(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
