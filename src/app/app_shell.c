/**
 * @file app_shell.c
 * @brief 应用层 Zephyr Shell 命令（app status/kv/...）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include "app_config.h"
#include "app_kv.h"
#include "app_main.h"
#include "app_version.h"
#include "event_dispatcher.h"
#include "event_system_compat.h"
#include "module_manager_compat.h"
#include "sys_log.h"
#include "sys_memory.h"
#include "sys_watchdog.h"

#ifdef CONFIG_DATA_BUS
#include "data_bus.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#ifdef CONFIG_SHELL

static int kv_join_argv(char* out, size_t out_sz, size_t argc, char** argv, size_t first_idx) {
    size_t pos = 0U;

    for (size_t i = first_idx; i < argc; i++) {
        size_t len = strlen(argv[i]);

        if (pos > 0U) {
            if (pos + 1U >= out_sz) {
                return -ENOSPC;
            }
            out[pos++] = ' ';
        }
        if (pos + len >= out_sz) {
            return -ENOSPC;
        }
        memcpy(out + pos, argv[i], len);
        pos += len;
    }
    out[pos] = '\0';
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
    shell_print(shell, "  State: %s", app_is_running() ? "RUNNING" : "STOPPED");
    shell_print(shell, "  Uptime: %d ms", app_get_uptime());
    shell_print(shell, "  Heartbeats: %d", (int) app_get_heartbeat_count());

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

static void cmd_app_top_once(const struct shell* shell) {
    module_compat_stats_t module_stats;
    event_compat_stats_t  event_stats;

    module_compat_get_stats(&module_stats);
    event_compat_get_statistics(&event_stats);

    shell_print(shell, "Top Snapshot:");
    shell_print(shell, "  Uptime: %d ms  State: %s", app_get_uptime(), app_is_running() ? "RUNNING" : "STOPPED");
    shell_print(shell, "  Modules: total=%u active=%u errors=%u events=%u dropped=%u",
                (unsigned int) module_stats.total_modules, (unsigned int) module_stats.active_modules,
                (unsigned int) module_stats.error_modules, (unsigned int) module_stats.events_processed,
                (unsigned int) module_stats.events_dropped);
    shell_print(shell, "  Events: total=%u queue=%u dropped=%u", (unsigned int) event_stats.total_events,
                (unsigned int) event_stats.queue_depth, (unsigned int) event_stats.dropped_events);
    shell_print(shell, "  Memory: free=%d min_free=%d heap=%d", sys_mem_get_free_size(), sys_mem_get_min_free_size(),
                sys_mem_get_heap_size());

#ifdef CONFIG_DATA_BUS
    data_bus_overview_t data_bus_stats;
    data_bus_get_overview(&data_bus_stats);
    shell_print(shell,
                "  DataBus: channels=%u active=%u queue=%u pub=%u drop=%u alloc_fail=%u malloc_fb=%u slab_exh=%u",
                (unsigned int) data_bus_stats.channel_count, (unsigned int) data_bus_stats.active_channel_count,
                (unsigned int) data_bus_stats.total_queue_used, (unsigned int) data_bus_stats.total_publish_count,
                (unsigned int) data_bus_stats.total_drop_count, (unsigned int) data_bus_stats.total_alloc_fail_count,
                (unsigned int) data_bus_stats.total_malloc_fallback_count,
                (unsigned int) data_bus_stats.total_slab_exhausted_count);
#else
    shell_print(shell, "  DataBus: disabled");
#endif

#ifdef CONFIG_THREAD_IPC_SERVICE
    shell_print(shell, "  IPC: enabled (per-service pending/shm stats)");
#else
    shell_print(shell, "  IPC: disabled");
#endif

#ifdef CONFIG_SYS_WATCHDOG_ENABLE
    wdt_stats_t wdt_stats;
    sys_wdt_get_stats(&wdt_stats);
    shell_print(shell, "  Watchdog: feeds=%u warnings=%u expires=%u since_feed=%u ms max_interval=%u ms",
                (unsigned int) wdt_stats.feed_count, (unsigned int) wdt_stats.warning_count,
                (unsigned int) wdt_stats.expire_count, (unsigned int) sys_wdt_get_time_since_feed(),
                (unsigned int) wdt_stats.max_feed_interval_ms);
#else
    shell_print(shell, "  Watchdog: disabled");
#endif
}

static int cmd_app_top(const struct shell* shell, size_t argc, char** argv) {
    int count = 1;

    if (argc > 1) {
        count = atoi(argv[1]);
        if (count <= 0) {
            shell_print(shell, "Usage: app top [count]");
            return -EINVAL;
        }
        if (count > 60) {
            count = 60;
        }
    }

    for (int i = 0; i < count; i++) {
        cmd_app_top_once(shell);
        if (i + 1 < count) {
            k_sleep(K_SECONDS(1));
        }
    }

    return 0;
}

static int cmd_app_log(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_LOG_DUMP
    shell_print(shell, "Log dump disabled (enable CONFIG_APP_ENABLE_LOG_DUMP in prj.conf or overlay)");
    return 0;
#else
    if (argc > 1) {
        int level = atoi(argv[1]);

        if (level < SYS_LOG_LEVEL_OFF) {
            level = SYS_LOG_LEVEL_OFF;
        } else if (level >= SYS_LOG_LEVEL_MAX) {
            level = SYS_LOG_LEVEL_DBG;
        }
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
    shell_print(shell, "  app top [n]    - Show compact runtime snapshot");
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
                               SHELL_CMD(top, NULL, "Show compact runtime snapshot [count]", cmd_app_top),
                               SHELL_CMD(log, NULL, "Dump log buffer [level]", cmd_app_log),
                               SHELL_CMD(kv, &sub_app_kv, "String key/value store (RAM)", NULL),
                               SHELL_CMD(help, NULL, "Show application help", cmd_app_help), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);

#endif /* CONFIG_SHELL */
