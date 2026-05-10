/**
 * @file sys_log.h
 * @brief 系统日志服务头文件
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
#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <zephyr/logging/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Log Level Definitions
 * ============================================================================= */

typedef enum {
    SYS_LOG_LEVEL_OFF = 0,
    SYS_LOG_LEVEL_ERR = 1,
    SYS_LOG_LEVEL_WRN = 2,
    SYS_LOG_LEVEL_INF = 3,
    SYS_LOG_LEVEL_DBG = 4,
    SYS_LOG_LEVEL_MAX = 5
} sys_log_level_t;

/* =============================================================================
 * Log destination (bitmask; OR together)
 * ============================================================================= */

typedef uint32_t sys_log_dest_mask_t;

#define SYS_LOG_DEST_CONSOLE (1U << 0)
#define SYS_LOG_DEST_MEMORY  (1U << 1)
#define SYS_LOG_DEST_RTT     (1U << 2) /* SEGGER RTT when CONFIG_SEGGER_RTT */
#define SYS_LOG_DEST_UART    (1U << 3) /* Same printk path as console on typical boards */
#define SYS_LOG_DEST_ALL     0xFFu

/* =============================================================================
 * Log Entry Structure
 * ============================================================================= */

#define SYS_LOG_MSG_MAX_LEN  128

typedef struct {
    uint32_t        timestamp;
    sys_log_level_t level;
    char            module_name[32]; /* SIL-2: 存储模块名称副本而非指针 */
    char            message[SYS_LOG_MSG_MAX_LEN];
} sys_log_entry_t;

/* =============================================================================
 * Configuration
 * ============================================================================= */

typedef struct {
    sys_log_level_t     default_level;
    sys_log_dest_mask_t destinations;
    bool                enable_timestamp;
    bool                enable_colors;
    bool                enable_module_name;
    /** 环形内存日志区字节数，用于推导实际使用的条目数（不超过静态缓冲上限） */
    uint32_t            memory_buffer_size;
} sys_log_config_t;

/* =============================================================================
 * API Functions
 * ============================================================================= */

/**
 * @brief Initialize logging system
 * @param config Configuration structure
 * @return 0 on success, negative error code on failure
 */
int sys_log_init(const sys_log_config_t* config);

/**
 * @brief Set log level for a module
 * @param module Module name
 * @param level Log level
 */
void sys_log_set_level(const char* module, sys_log_level_t level);

/**
 * @brief Get current log level
 * @param module Module name
 * @return Current log level
 */
sys_log_level_t sys_log_get_level(const char* module);

/**
 * @brief Enable/disable log destination(s)
 * @param dest Bitmask (one or more SYS_LOG_DEST_* bits, or SYS_LOG_DEST_ALL)
 * @param enable true to enable, false to disable
 */
void sys_log_set_destination(sys_log_dest_mask_t dest, bool enable);

/**
 * @brief Log a message
 * @param level Log level
 * @param module Module name
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void sys_log_print(sys_log_level_t level, const char* module, const char* format, ...);

/**
 * @brief Log a message with timestamp
 * @param level Log level
 * @param module Module name
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void sys_log_print_ts(sys_log_level_t level, const char* module, const char* format, ...);

/**
 * @brief Log binary data
 * @param level Log level
 * @param module Module name
 * @param data Pointer to data
 * @param len Data length
 * @param ascii Show ASCII representation
 */
void sys_log_hexdump(sys_log_level_t level, const char* module, const void* data, size_t len, bool ascii);

/**
 * @brief Get log entries from memory buffer
 * @param entries Output buffer for entries
 * @param count Number of entries to retrieve
 * @param oldest_first true to get oldest entries first
 * @return Number of entries retrieved
 */
uint32_t sys_log_get_entries(sys_log_entry_t* entries, uint32_t count, bool oldest_first);

/**
 * @brief Clear log memory buffer
 */
void sys_log_clear_buffer(void);

/**
 * @brief 获取当前环形缓冲中的日志条数（非累计写入次数）
 *
 * @return 当前环内有效条目数（与 sys_log_clear_buffer 后归零的 count 一致）
 */
uint32_t sys_log_get_count(void);

/**
 * @brief Dump logs to console (requires CONSOLE and/or UART destination enabled)
 * @param level_filter Minimum level to display
 */
void sys_log_dump(sys_log_level_t level_filter);

/* =============================================================================
 * Convenience Macros
 * ============================================================================= */

#define LOG_E(module, fmt, ...)          sys_log_print(SYS_LOG_LEVEL_ERR, module, fmt, ##__VA_ARGS__)

#define LOG_W(module, fmt, ...)          sys_log_print(SYS_LOG_LEVEL_WRN, module, fmt, ##__VA_ARGS__)

#define LOG_I(module, fmt, ...)          sys_log_print(SYS_LOG_LEVEL_INF, module, fmt, ##__VA_ARGS__)

#define LOG_D(module, fmt, ...)          sys_log_print(SYS_LOG_LEVEL_DBG, module, fmt, ##__VA_ARGS__)

#define LOG_HEXDUMP_E(module, data, len) sys_log_hexdump(SYS_LOG_LEVEL_ERR, module, data, len, true)

#define LOG_HEXDUMP_I(module, data, len) sys_log_hexdump(SYS_LOG_LEVEL_INF, module, data, len, true)

#ifdef __cplusplus
}
#endif

#endif /* SYS_LOG_H */
