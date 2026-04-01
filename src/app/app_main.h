/**
 * @file app_main.h
 * @brief Application Main Header
 *
 * Main application entry point and initialization.
 *
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Application Version
 * ============================================================================= */

#define APP_VERSION_MAJOR  1
#define APP_VERSION_MINOR  0
#define APP_VERSION_PATCH  0
#define APP_VERSION_STRING "1.0.0"

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
 * @brief Start application
 * @return 0 on success, negative error code on failure
 */
int app_start(void);

/**
 * @brief Stop application
 * @return 0 on success, negative error code on failure
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
