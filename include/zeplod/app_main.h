/**
 * @file app_main.h
 * @brief 应用入口 API 与配置类型（版本信息见 app_version.h）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <zeplod/app_config.h>
#include <zeplod/app_version.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 应用配置
 * ============================================================================= */

typedef struct {
    bool     enable_logging;
    bool     enable_watchdog;
    bool     enable_shell;
    uint32_t log_level;
} app_config_t;

/* =============================================================================
 * 应用 API
 * ============================================================================= */

/**
 * @brief 查询 SYS_INIT 是否已完成，并可选应用运行时配置
 *
 * 子系统在 main 之前由 SYS_INIT 初始化。若 config 非 NULL，更新 g_app.config 并
 * 应用日志级别等（见 app_apply_runtime_config）；须在 app_start() 之前调用。
 *
 * @param config NULL 表示仅检查初始化状态
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
 * @brief 获取自 app_start() 成功以来的运行时间
 * @return 运行时间（毫秒）；未 start 时返回 0
 */
uint32_t app_get_uptime(void);

/**
 * @brief 检查应用是否正在运行
 * @return 正在运行返回 true，否则返回 false
 */
bool app_is_running(void);

/**
 * @brief 获取应用心跳计数
 * @return 心跳计数
 */
uint32_t app_get_heartbeat_count(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
