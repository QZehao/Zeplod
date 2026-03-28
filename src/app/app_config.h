/**
 * @file app_config.h
 * @brief Application Configuration Header
 * 
 * Centralized application configuration and feature flags.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Feature Flags
 * ============================================================================= */

/* Enable/disable modules */
#define APP_CONFIG_ENABLE_MODULE_A    1
#define APP_CONFIG_ENABLE_MODULE_B    1

/* Enable/disable services */
#define APP_CONFIG_ENABLE_LOGGING     1
#define APP_CONFIG_ENABLE_WATCHDOG    1
#define APP_CONFIG_ENABLE_MEMORY_MGR  1
#define APP_CONFIG_ENABLE_TIMER_SVC   1

/* Debug features */
#define APP_CONFIG_ENABLE_SHELL       1
#define APP_CONFIG_ENABLE_STATS       1
#define APP_CONFIG_ENABLE_LOG_DUMP    1

/* =============================================================================
 * System Configuration
 * ============================================================================= */

/* Task priorities (lower number = higher priority) */
#define APP_PRIORITY_EVENT_DISPATCHER   5
#define APP_PRIORITY_MODULE_HIGH        2
#define APP_PRIORITY_MODULE_NORMAL      5
#define APP_PRIORITY_MODULE_LOW         8

/* Stack sizes */
#define APP_STACK_MAIN                  4096
#define APP_STACK_EVENT_DISPATCHER      2048
#define APP_STACK_MODULE_DEFAULT        1024

/* Timing configuration */
#define APP_TICK_RATE_HZ                1000
#define APP_WATCHDOG_TIMEOUT_MS         5000
#define APP_HEARTBEAT_INTERVAL_MS       1000

/* Memory configuration */
#define APP_HEAP_SIZE                   16384
#define APP_EVENT_QUEUE_SIZE            64
#define APP_MAX_MODULES                 16

/* =============================================================================
 * Event Type Definitions
 * ============================================================================= */

/* Reserved event types (0-9 are system events) */
#define APP_EVENT_TYPE_SYSTEM           0
#define APP_EVENT_TYPE_ERROR            1
#define APP_EVENT_TYPE_CONFIG           2

/* Application event types (10+) */
#define APP_EVENT_TYPE_USER_START       10

/* =============================================================================
 * Error Codes
 * ============================================================================= */

#define APP_OK                          0
#define APP_ERR_INIT                   -1
#define APP_ERR_MEMORY                 -2
#define APP_ERR_TIMEOUT                -3
#define APP_ERR_INVALID_PARAM          -4
#define APP_ERR_NOT_FOUND              -5
#define APP_ERR_BUSY                   -6
#define APP_ERR_DISABLED               -7

/* =============================================================================
 * Build Configuration
 * ============================================================================= */

/* Version information */
#define APP_BUILD_VERSION_MAJOR         PROJECT_VERSION_MAJOR
#define APP_BUILD_VERSION_MINOR         PROJECT_VERSION_MINOR
#define APP_BUILD_VERSION_PATCH         PROJECT_VERSION_PATCH

/* Build type */
#ifdef NDEBUG
#define APP_BUILD_TYPE                  "Release"
#else
#define APP_BUILD_TYPE                  "Debug"
#endif

/* Target information (set by build system) */
#ifndef APP_TARGET_NAME
#define APP_TARGET_NAME                 "generic"
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
