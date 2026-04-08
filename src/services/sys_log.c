/**
 * @file sys_log.c
 * @brief 系统日志服务实现
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
#include "sys_log.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_SEGGER_RTT)
#include <SEGGER_RTT.h>
#endif

LOG_MODULE_REGISTER(sys_log, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * SIL-2: 配置验证宏
 * ============================================================================= */

/** 最小日志缓冲区大小 */
#ifndef SYS_LOG_MIN_BUFFER_SIZE
#define SYS_LOG_MIN_BUFFER_SIZE 4U
#endif

/** 最大日志缓冲区大小 */
#ifndef SYS_LOG_MAX_BUFFER_SIZE
#define SYS_LOG_MAX_BUFFER_SIZE 1024U
#endif

/** 最大模块名称长度 */
#ifndef SYS_LOG_MAX_MODULE_NAME_LEN
#define SYS_LOG_MAX_MODULE_NAME_LEN 32U
#endif

/** 目的地数组大小 */
#define SYS_LOG_DEST_COUNT 4U

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#ifndef CONFIG_SYS_MEMORY_POOL_SIZE
#define CONFIG_SYS_MEMORY_POOL_SIZE 8192
#endif

#define MAX_LOG_ENTRIES (CONFIG_SYS_MEMORY_POOL_SIZE / sizeof(sys_log_entry_t))

/* ANSI color codes */
#define COLOR_RED       "\x1b[31m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_BLUE      "\x1b[34m"
#define COLOR_RESET     "\x1b[0m"

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    sys_log_config_t config;
    sys_log_entry_t* buffer;
    uint32_t         write_idx;
    uint32_t         read_idx;
    uint32_t         count;
    uint32_t         total_count;
    struct k_mutex   lock;
    sys_log_level_t  module_levels[16]; /* Per-module levels */
    bool             destinations_enabled[4];
} sys_log_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static sys_log_cb_t    g_sys_log;
static sys_log_entry_t g_log_buffer_static[MAX_LOG_ENTRIES];

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static const char* level_to_string(sys_log_level_t level) {
    switch (level) {
    case SYS_LOG_LEVEL_ERR:
        return "ERROR";
    case SYS_LOG_LEVEL_WRN:
        return "WARN";
    case SYS_LOG_LEVEL_INF:
        return "INFO";
    case SYS_LOG_LEVEL_DBG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

static const char* level_to_color(sys_log_level_t level) {
    if (!g_sys_log.config.enable_colors) {
        return "";
    }

    switch (level) {
    case SYS_LOG_LEVEL_ERR:
        return COLOR_RED;
    case SYS_LOG_LEVEL_WRN:
        return COLOR_YELLOW;
    case SYS_LOG_LEVEL_INF:
        return COLOR_GREEN;
    case SYS_LOG_LEVEL_DBG:
        return COLOR_BLUE;
    default:
        return COLOR_RESET;
    }
}

static void apply_destinations_mask(uint32_t mask) {
    memset(g_sys_log.destinations_enabled, 0, sizeof(g_sys_log.destinations_enabled));

    if (mask == 0U) {
        mask = SYS_LOG_DEST_CONSOLE | SYS_LOG_DEST_MEMORY;
    }
    if (mask == SYS_LOG_DEST_ALL || mask == 0xFFu) {
        mask = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3);
    }
    for (int i = 0; i < 4; i++) {
        if (mask & (1U << i)) {
            g_sys_log.destinations_enabled[i] = true;
        }
    }
}

static void add_entry(sys_log_level_t level, const char* module, const char* msg, uint32_t timestamp) {
    k_mutex_lock(&g_sys_log.lock, K_FOREVER);

    sys_log_entry_t* entry = &g_sys_log.buffer[g_sys_log.write_idx];

    entry->timestamp = timestamp;
    entry->level = level;
    entry->module = module;
    strncpy(entry->message, msg, SYS_LOG_MSG_MAX_LEN - 1);
    entry->message[SYS_LOG_MSG_MAX_LEN - 1] = '\0';

    g_sys_log.write_idx = (g_sys_log.write_idx + 1) % MAX_LOG_ENTRIES;

    if (g_sys_log.count < MAX_LOG_ENTRIES) {
        g_sys_log.count++;
    } else {
        /* Buffer full, advance read index */
        g_sys_log.read_idx = (g_sys_log.read_idx + 1) % MAX_LOG_ENTRIES;
    }

    g_sys_log.total_count++;

    k_mutex_unlock(&g_sys_log.lock);
}

static void output_to_printk(sys_log_level_t level, const char* module, const char* msg, uint32_t timestamp) {
    if (!g_sys_log.destinations_enabled[0] && !g_sys_log.destinations_enabled[3]) {
        return;
    }

    const char* color = level_to_color(level);
    const char* reset = g_sys_log.config.enable_colors ? COLOR_RESET : "";

    if (g_sys_log.config.enable_timestamp) {
        printk("%s[%08d]%s ", color, timestamp, reset);
    }

    if (g_sys_log.config.enable_module_name && module != NULL) {
        printk("%s[%s]%s ", color, module, reset);
    }

    printk("%s%s%s\n", color, msg, reset);
}

#if defined(CONFIG_SEGGER_RTT)
static void output_to_rtt(sys_log_level_t level, const char* module, const char* msg, uint32_t timestamp) {
    ARG_UNUSED(level);

    if (!g_sys_log.destinations_enabled[2]) {
        return;
    }

    char buf[SYS_LOG_MSG_MAX_LEN + 96];
    int  n = snprintf(buf, sizeof(buf), "[%08u][%s] %s\n", timestamp, module != NULL ? module : "-", msg);
    if (n > 0) {
        SEGGER_RTT_Write(0, buf, (unsigned) MIN((size_t) n, sizeof(buf)));
    }
}
#endif

static void emit_log_line(sys_log_level_t level, const char* module, const char* msg, uint32_t timestamp) {
    if (g_sys_log.destinations_enabled[1]) {
        add_entry(level, module, msg, timestamp);
    }

    output_to_printk(level, module, msg, timestamp);

#if defined(CONFIG_SEGGER_RTT)
    output_to_rtt(level, module, msg, timestamp);
#endif
}

/* =============================================================================
 * API Implementation
 * ============================================================================= */

int sys_log_init(const sys_log_config_t* config) {
    LOG_INF("Initializing system log...");

    /* SIL-2: 清零全局控制块 */
    memset(&g_sys_log, 0, sizeof(g_sys_log));

    /* Set default config */
    if (config != NULL) {
        /* SIL-2: 验证配置参数 */
        if (config->memory_buffer_size > 0 && 
            config->memory_buffer_size < sizeof(sys_log_entry_t)) {
            LOG_ERR("Invalid memory_buffer_size: %u", config->memory_buffer_size);
            return -EINVAL;
        }
        g_sys_log.config = *config;
    } else {
        g_sys_log.config.default_level = SYS_LOG_LEVEL_INF;
        g_sys_log.config.destinations = SYS_LOG_DEST_CONSOLE | SYS_LOG_DEST_MEMORY;
        g_sys_log.config.enable_timestamp = true;
        g_sys_log.config.enable_colors = true;
        g_sys_log.config.enable_module_name = true;
        g_sys_log.config.memory_buffer_size = CONFIG_SYS_MEMORY_POOL_SIZE;
    }

    /* SIL-2: 验证MAX_LOG_ENTRIES合理性 */
    if (MAX_LOG_ENTRIES < SYS_LOG_MIN_BUFFER_SIZE || MAX_LOG_ENTRIES > SYS_LOG_MAX_BUFFER_SIZE) {
        LOG_WRN("MAX_LOG_ENTRIES %u outside reasonable range [%u, %u]",
                MAX_LOG_ENTRIES, SYS_LOG_MIN_BUFFER_SIZE, SYS_LOG_MAX_BUFFER_SIZE);
    }

    /* Initialize buffer */
    g_sys_log.buffer = g_log_buffer_static;
    g_sys_log.write_idx = 0;
    g_sys_log.read_idx = 0;
    g_sys_log.count = 0;
    g_sys_log.total_count = 0;

    k_mutex_init(&g_sys_log.lock);

    apply_destinations_mask(g_sys_log.config.destinations);

    /* Initialize module levels */
    for (int i = 0; i < 16; i++) {
        g_sys_log.module_levels[i] = g_sys_log.config.default_level;
    }

    LOG_INF("System log initialized");
    return 0;
}

void sys_log_set_level(const char* module, sys_log_level_t level) {
    /* For simplicity, set global level */
    g_sys_log.config.default_level = level;

    /* Set all module levels */
    for (int i = 0; i < 16; i++) {
        g_sys_log.module_levels[i] = level;
    }
}

sys_log_level_t sys_log_get_level(const char* module) {
    return g_sys_log.config.default_level;
}

void sys_log_set_destination(sys_log_dest_mask_t dest, bool enable) {
    if (dest == SYS_LOG_DEST_ALL) {
        for (int i = 0; i < 4; i++) {
            g_sys_log.destinations_enabled[i] = enable;
        }
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (dest & (1U << i)) {
            g_sys_log.destinations_enabled[i] = enable;
        }
    }
}

void sys_log_print(sys_log_level_t level, const char* module, const char* format, ...) {
    if (level > g_sys_log.config.default_level) {
        return;
    }

    char    msg[SYS_LOG_MSG_MAX_LEN];
    va_list args;

    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    uint32_t ts = k_uptime_get_32();
    emit_log_line(level, module, msg, ts);
}

void sys_log_print_ts(sys_log_level_t level, const char* module, const char* format, ...) {
    if (level > g_sys_log.config.default_level) {
        return;
    }

    char    msg[SYS_LOG_MSG_MAX_LEN];
    va_list args;

    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    uint32_t ts = k_uptime_get_32();
    emit_log_line(level, module, msg, ts);
}

void sys_log_hexdump(sys_log_level_t level, const char* module, const void* data, size_t len, bool ascii) {
    if (level > g_sys_log.config.default_level) {
        return;
    }

    const uint8_t* bytes = (const uint8_t*) data;
    char           line[80];

    for (size_t i = 0; i < len; i += 16) {
        size_t chunk = MIN(16, len - i);
        int    pos = 0;

        /* Address */
        pos += snprintf(line + pos, sizeof(line) - pos, "%08X: ", (uint32_t) i);

        /* Hex bytes */
        for (size_t j = 0; j < 16; j++) {
            if (j < chunk) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", bytes[i + j]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
            if (j == 7)
                pos += snprintf(line + pos, sizeof(line) - pos, " ");
        }

        /* ASCII */
        if (ascii) {
            pos += snprintf(line + pos, sizeof(line) - pos, " |");
            for (size_t j = 0; j < chunk; j++) {
                char c = bytes[i + j];
                pos += snprintf(line + pos, sizeof(line) - pos, "%c", (c >= 32 && c < 127) ? c : '.');
            }
            pos += snprintf(line + pos, sizeof(line) - pos, "|");
        }

        /* Log the line */
        sys_log_print(level, module, "%s", line);
    }
}

uint32_t sys_log_get_entries(sys_log_entry_t* entries, uint32_t count, bool oldest_first) {
    if (entries == NULL || count == 0) {
        return 0;
    }

    k_mutex_lock(&g_sys_log.lock, K_FOREVER);

    uint32_t available = g_sys_log.count;
    uint32_t to_read = MIN(count, available);

    if (to_read == 0) {
        k_mutex_unlock(&g_sys_log.lock);
        return 0;
    }

    if (oldest_first) {
        /* Start from read index */
        for (uint32_t i = 0; i < to_read; i++) {
            uint32_t idx = (g_sys_log.read_idx + i) % MAX_LOG_ENTRIES;
            entries[i] = g_sys_log.buffer[idx];
        }
    } else {
        /* Start from write index - 1 (newest first) */
        for (uint32_t i = 0; i < to_read; i++) {
            int32_t idx = (int32_t) g_sys_log.write_idx - 1 - i;
            if (idx < 0)
                idx += MAX_LOG_ENTRIES;
            entries[i] = g_sys_log.buffer[(uint32_t) idx];
        }
    }

    k_mutex_unlock(&g_sys_log.lock);
    return to_read;
}

void sys_log_clear_buffer(void) {
    k_mutex_lock(&g_sys_log.lock, K_FOREVER);
    g_sys_log.write_idx = 0;
    g_sys_log.read_idx = 0;
    g_sys_log.count = 0;
    k_mutex_unlock(&g_sys_log.lock);
}

uint32_t sys_log_get_count(void) {
    return g_sys_log.total_count;
}

void sys_log_dump(sys_log_level_t level_filter) {
    if (!g_sys_log.destinations_enabled[0] && !g_sys_log.destinations_enabled[3]) {
        return;
    }

    sys_log_entry_t entries[32];
    uint32_t        retrieved;

    printk("\n=== Log Dump (min level: %d) ===\n", level_filter);

    do {
        retrieved = sys_log_get_entries(entries, 32, true);
        for (uint32_t i = 0; i < retrieved; i++) {
            if (entries[i].level >= level_filter) {
                printk("[%08d][%s][%s] %s\n", entries[i].timestamp, level_to_string(entries[i].level),
                       entries[i].module != NULL ? entries[i].module : "N/A", entries[i].message);
            }
        }
    } while (retrieved == 32);

    printk("=== End Log Dump ===\n\n");
}
