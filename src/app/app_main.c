/**
 * @file app_main.c
 * @brief
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

#include "app_main.h"
#include "app_kv.h"
#include "event_dispatcher.h"
#include "event_system.h"
#include "event_system_compat.h"
#include "module_base.h"
#include "module_manager.h"
#include "module_manager_compat.h"
#include "sys_log.h"
#include "sys_memory.h"
#include "sys_timer.h"
#include "sys_watchdog.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app_main, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    app_config_t config;
    bool         initialized;
    bool         running;
    uint32_t     start_time;
    uint32_t     heartbeat_count;
} app_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static app_cb_t g_app;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void app_heartbeat_timer_callback(sys_timer_handle_t timer, void* user_data);
static void app_print_banner(void);

static int app_init_apply_cb(void);
#if APP_CONFIG_ENABLE_APP_KV
static int app_init_kv_step(void);
#endif
static int app_init_finalize(void);

SYS_INIT(app_init_apply_cb, POST_KERNEL, APP_INIT_PRIO_APP_CB);
#if APP_CONFIG_ENABLE_APP_KV
SYS_INIT(app_init_kv_step, POST_KERNEL, APP_INIT_PRIO_APP_KV);
#endif
/* 所有系统服务和事件系统已改为自动初始化机制（在各自的 .c 文件中） */
SYS_INIT(app_init_finalize, POST_KERNEL, APP_INIT_PRIO_APP_FINAL);

/* =============================================================================
 * Shell Commands
 * ============================================================================= */

#ifdef CONFIG_SHELL

static int kv_join_argv(char* out, size_t out_sz, size_t argc, char** argv, size_t first_idx) {
    size_t pos = 0U;
    out[0] = '\0';
    for (size_t i = first_idx; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (pos > 0U) {
            if (pos + 1U >= out_sz) {
                return -ENOSPC;
            }
            out[pos++] = ' ';
            out[pos] = '\0';
        }
        if (pos + len >= out_sz) {
            return -ENOSPC;
        }
        memcpy(out + pos, argv[i], len);
        pos += len;
        out[pos] = '\0';
    }
    return 0;
}

static int cmd_app_kv_set(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled (APP_CONFIG_ENABLE_APP_KV=0)");
    return 0;
#else
    if (argc < 2) {
        shell_print(shell, "Usage: app kv set <key> [<value>...]");
        return -EINVAL;
    }
    char val[APP_KV_VALUE_MAX_LEN];
    if (argc == 2) {
        if (app_kv_set(argv[1], "") != APP_OK) {
            shell_print(shell, "set failed");
            return -EIO;
        }
        return 0;
    }
    if (kv_join_argv(val, sizeof(val), argc, argv, 2) != 0) {
        shell_print(shell, "value too long");
        return -ENOSPC;
    }
    int r = app_kv_set(argv[1], val);
    if (r == APP_ERR_KV_FULL) {
        shell_print(shell, "kv full (max %d entries)", APP_KV_MAX_ENTRIES);
        return -ENOMEM;
    }
    if (r != APP_OK) {
        shell_print(shell, "set failed (%d)", r);
        return -EIO;
    }
    return 0;
#endif
}

static int cmd_app_kv_get(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#else
    if (argc < 2) {
        shell_print(shell, "Usage: app kv get <key>");
        return -EINVAL;
    }
    char buf[APP_KV_VALUE_MAX_LEN];
    int  r = app_kv_get(argv[1], buf, sizeof(buf));
    if (r == APP_ERR_NOT_FOUND) {
        shell_print(shell, "(not found)");
        return 0;
    }
    if (r != APP_OK) {
        shell_print(shell, "get failed (%d)", r);
        return -EIO;
    }
    shell_print(shell, "%s", buf);
    return 0;
#endif
}

static int cmd_app_kv_del(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#else
    if (argc < 2) {
        shell_print(shell, "Usage: app kv del <key>");
        return -EINVAL;
    }
    int r = app_kv_remove(argv[1]);
    if (r == APP_ERR_NOT_FOUND) {
        shell_print(shell, "(not found)");
        return 0;
    }
    return r == APP_OK ? 0 : -EIO;
#endif
}

static int cmd_app_kv_list_cb(const char* k, const char* v, void* user) {
    shell_print((const struct shell*) user, "  %s = %s", k, v);
    return 0;
}

static int cmd_app_kv_list(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#else
    shell_print(shell, "KV entries: %u / %d", (unsigned) app_kv_count(), APP_KV_MAX_ENTRIES);
    (void) app_kv_foreach(cmd_app_kv_list_cb, (void*) shell);
    return 0;
#endif
}

static int cmd_app_kv_clear(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#else
    app_kv_clear();
    shell_print(shell, "ok");
    return 0;
#endif
}

static int cmd_app_kv_save(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#elif !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    shell_print(shell, "persist off (CONFIG_APP_KV_PERSIST=n)");
    return 0;
#else
    int r = app_kv_save();
    shell_print(shell, "%s (%d)", r == APP_OK ? "saved" : "save failed", r);
    return r == APP_OK ? 0 : -EIO;
#endif
}

static int cmd_app_kv_load(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if !APP_CONFIG_ENABLE_APP_KV
    shell_print(shell, "app_kv disabled");
    return 0;
#elif !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    shell_print(shell, "persist off (CONFIG_APP_KV_PERSIST=n)");
    return 0;
#else
    int r = app_kv_load();
    shell_print(shell, "%s (%d)", r == APP_OK ? "loaded" : "load failed", r);
    return r == APP_OK ? 0 : -EIO;
#endif
}

static int cmd_app_status(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    char version_str[VERSION_STRING_MAX_LEN];
    char info_str[VERSION_INFO_STRING_MAX_LEN];

    if (app_version_get_string(version_str, sizeof(version_str)) != APP_OK ||
        app_version_get_info_string(info_str, sizeof(info_str)) != APP_OK) {
        shell_print(shell, "version string error");
        return -EIO;
    }

    shell_print(shell, "Application Status:");
    shell_print(shell, "  Version: %s", version_str);
    shell_print(shell, "  Info: %s", info_str);
    shell_print(shell, "  State: %s", g_app.running ? "RUNNING" : "STOPPED");
    shell_print(shell, "  Uptime: %d ms", app_get_uptime());
    shell_print(shell, "  Heartbeats: %d", g_app.heartbeat_count);

    return 0;
}

static int cmd_app_modules(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(shell, "Registered Modules:");
    module_compat_dump_info();

    module_compat_stats_t stats;
    module_compat_get_stats(&stats);
    shell_print(shell, "Module Statistics:");
    shell_print(shell, "  Total: %d", stats.total_modules);
    shell_print(shell, "  Active: %d", stats.active_modules);
    shell_print(shell, "  Errors: %d", stats.error_modules);

    return 0;
}

static int cmd_app_events(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    event_compat_stats_t stats;
    event_compat_get_statistics(&stats);

    shell_print(shell, "Event System Statistics:");
    shell_print(shell, "  Total Events: %d", stats.total_events);
    shell_print(shell, "  Queue Depth: %d", stats.queue_depth);
    shell_print(shell, "  Dropped: %d", stats.dropped_events);

#if APP_CONFIG_ENABLE_STATS
    dispatcher_stats_t dstats;
    event_dispatcher_get_stats(&dstats);
    shell_print(shell, "Dispatcher Statistics:");
    shell_print(shell, "  Processed: %llu", (unsigned long long) dstats.events_processed);
    shell_print(shell, "  Dropped: %llu", (unsigned long long) dstats.events_dropped);
    shell_print(shell, "  Max latency (us): %u", dstats.max_latency_us);
    shell_print(shell, "  Avg latency (us): %u", dstats.avg_latency_us);
    shell_print(shell, "  Processing errors: %u", dstats.processing_errors);
#endif

    return 0;
}

static int cmd_app_memory(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(shell, "Memory Statistics:");
    shell_print(shell, "  Heap Size: %d bytes", sys_mem_get_heap_size());
    shell_print(shell, "  Free: %d bytes", sys_mem_get_free_size());
    shell_print(shell, "  Min Free: %d bytes", sys_mem_get_min_free_size());

    return 0;
}

static int cmd_app_log(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_LOG_DUMP
    shell_print(shell, "Log dump disabled (enable CONFIG_APP_ENABLE_LOG_DUMP in prj.conf or overlay)");
    return 0;
#else
    if (argc > 1) {
        int level = atoi(argv[1]);
        sys_log_dump((sys_log_level_t) level);
    } else {
        sys_log_dump(SYS_LOG_LEVEL_INF);
    }

    return 0;
#endif
}

static int cmd_app_help(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(shell, "Available commands:");
    shell_print(shell, "  app status     - Show application status");
    shell_print(shell, "  app modules    - Show module information");
    shell_print(shell, "  app events     - Show event statistics");
    shell_print(shell, "  app memory     - Show memory statistics");
    shell_print(shell, "  app log [lvl]  - Dump log buffer");
    shell_print(shell, "  app kv ...     - Key-value (set/get/del/list/clear; save/load if persist)");
    shell_print(shell, "  app help       - Show this help");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_app_kv, SHELL_CMD(set, NULL, "Set key [value words...]", cmd_app_kv_set),
                               SHELL_CMD(get, NULL, "Get key", cmd_app_kv_get),
                               SHELL_CMD(del, NULL, "Delete key", cmd_app_kv_del),
                               SHELL_CMD(list, NULL, "List all entries", cmd_app_kv_list),
                               SHELL_CMD(clear, NULL, "Remove all entries", cmd_app_kv_clear),
                               SHELL_CMD(save, NULL, "Write KV to flash (CONFIG_APP_KV_PERSIST)", cmd_app_kv_save),
                               SHELL_CMD(load, NULL, "Reload KV from flash", cmd_app_kv_load), SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_app, SHELL_CMD(status, NULL, "Show application status", cmd_app_status),
                               SHELL_CMD(modules, NULL, "Show registered modules", cmd_app_modules),
                               SHELL_CMD(events, NULL, "Show event statistics", cmd_app_events),
                               SHELL_CMD(memory, NULL, "Show memory statistics", cmd_app_memory),
                               SHELL_CMD(log, NULL, "Dump log buffer [level]", cmd_app_log),
                               SHELL_CMD(kv, &sub_app_kv, "String key/value store (RAM)", NULL),
                               SHELL_CMD(help, NULL, "Show application help", cmd_app_help), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);

#endif /* CONFIG_SHELL */

/* =============================================================================
 * Auto-init steps (SYS_INIT, POST_KERNEL; order via APP_INIT_PRIO_*)
 * ============================================================================= */

static void app_apply_config(const app_config_t* config) {
    if (config != NULL) {
        g_app.config = *config;
    } else {
        g_app.config.enable_logging = APP_CONFIG_ENABLE_LOGGING;
        g_app.config.enable_watchdog = APP_CONFIG_ENABLE_WATCHDOG;
        g_app.config.enable_shell = APP_CONFIG_ENABLE_SHELL;
        g_app.config.log_level = CONFIG_SYS_LOG_LEVEL;
    }
}

static int app_init_apply_cb(void) {
    LOG_INF("========================================");
    LOG_INF("Application Initializing...");
    LOG_INF("========================================");
    app_version_print();

    memset(&g_app, 0, sizeof(g_app));
    app_apply_config(NULL);

    return 0;
}

#if APP_CONFIG_ENABLE_APP_KV
static int app_init_kv_step(void) {
    app_kv_init();
    (void) app_kv_set("build.target", APP_TARGET_NAME);
    return 0;
}
#endif

static int app_init_finalize(void) {
    g_app.initialized = true;
    g_app.start_time = k_uptime_get_32();
    LOG_INF("Application initialization complete");
    return 0;
}

/* =============================================================================
 * Application API Implementation
 * ============================================================================= */

int app_init(const app_config_t* config) {
    ARG_UNUSED(config);
    /* 子系统与模块由本文件及各模块内的 SYS_INIT(POST_KERNEL, APP_INIT_PRIO_*) 在 main 之前完成。 */
    return g_app.initialized ? APP_OK : APP_ERR_INIT;
}

int app_start(void) {
    if (!g_app.initialized) {
        LOG_ERR("Application not initialized");
        return APP_ERR_INIT;
    }

    if (g_app.running) {
        LOG_WRN("Application already running");
        return APP_OK;
    }

    LOG_INF("Starting application...");

    /* 事件系统和事件分发器已由各自的 SYS_INIT 自动启动，此处不再重复调用 */

    /* 模块管理器已由 module_manager_compat.c 的 SYS_INIT 自动启动，此处不再重复调用 */

    /* Start all registered modules */
    int started = module_compat_start_all();
    LOG_INF("Started %d modules", started);

    /* Start watchdog */
#if APP_CONFIG_ENABLE_WATCHDOG
    sys_wdt_start();
#endif

    /* Create heartbeat timer (only if timer service is enabled and interval > 0) */
#if APP_CONFIG_ENABLE_TIMER_SVC && (APP_HEARTBEAT_INTERVAL_MS > 0)
    sys_timer_config_t heartbeat_config = {.mode = SYS_TIMER_PERIODIC,
                                           .delay_ms = APP_HEARTBEAT_INTERVAL_MS,
                                           .period_ms = APP_HEARTBEAT_INTERVAL_MS,
                                           .callback = app_heartbeat_timer_callback,
                                           .user_data = NULL,
                                           .name = "heartbeat",
                                           .priority = APP_PRIORITY_MODULE_LOW};
    sys_timer_handle_t heartbeat = sys_timer_create(&heartbeat_config);
    if (heartbeat != NULL) {
        sys_timer_start(heartbeat);
    }
#endif

    g_app.running = true;

    app_print_banner();
    LOG_INF("Application started successfully");

    return APP_OK;
}

int app_stop(void) {
    if (!g_app.running) {
        return APP_OK;
    }

    LOG_INF("Stopping application...");

    g_app.running = false;

    /* Stop all modules */
    module_compat_stop_all();

    /* Stop event dispatcher before event system */
    if (event_dispatcher_stop() != EVENT_OK) {
        LOG_ERR("event_dispatcher_stop failed");
    } else {
        LOG_INF("Event dispatcher stopped");
    }

    /* Stop event system */
    if (event_compat_stop() != 0) {
        LOG_ERR("event_compat_stop failed");
    }

    /* Stop watchdog */
#if APP_CONFIG_ENABLE_WATCHDOG
    sys_wdt_stop();
#endif

    LOG_INF("Application stopped");
    return APP_OK;
}

uint32_t app_get_uptime(void) {
    if (!g_app.initialized) {
        return 0;
    }
    return k_uptime_get_32() - g_app.start_time;
}

bool app_is_running(void) {
    return g_app.running;
}

uint32_t app_get_heartbeat_count(void) {
    return g_app.heartbeat_count;
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void app_heartbeat_timer_callback(sys_timer_handle_t timer, void* user_data) {
    g_app.heartbeat_count++;

    /* Feed watchdog */
#if APP_CONFIG_ENABLE_WATCHDOG
    sys_wdt_feed();
#endif

    /* Log periodic status */
    if (g_app.heartbeat_count % 10 == 0) {
        LOG_INF("Heartbeat: %d, Uptime: %dms", g_app.heartbeat_count, app_get_uptime());
    }
}

static void app_print_banner(void) {
    LOG_INF("========================================");
    LOG_INF("  Zephyr Event-Driven Application");
    LOG_INF("  Version: %s", APP_VERSION_STRING);
    LOG_INF("========================================");
}

/* =============================================================================
 * Main Entry Point
 * ============================================================================= */

int main(void) {
    LOG_ERR("FW_MARKER: %s | %s", GIT_COMMIT_HASH, BUILD_TIMESTAMP);
    /* Initialize application */
    if (app_init(NULL) != APP_OK) {
        LOG_ERR("Application initialization failed");
        return -1;
    }

    /* Start application */
    if (app_start() != APP_OK) {
        LOG_ERR("Application start failed");
        return -1;
    }

    /* Main loop - in event-driven design, this is mostly idle */
    while (1) {
        /* Feed watchdog in main loop too */
#if APP_CONFIG_ENABLE_WATCHDOG
        sys_wdt_feed();
#endif

        /* Sleep to save power */
        k_msleep(1000);

        /* Could add main loop tasks here if needed */
    }

    /* Should not reach here */
    return 0;
}
