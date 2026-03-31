/**
 * @file app_main.c
 * @brief Application Main Implementation
 *
 * Main application entry point and initialization.
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "app_main.h"
#include "app_config.h"
#include "app_version.h"
#include "event_dispatcher.h"
#include "event_system.h"
#include "example_module_a.h"
#include "example_module_b.h"
#include "module_manager.h"
#include "sys_log.h"
#include "sys_memory.h"
#include "sys_timer.h"
#include "sys_watchdog.h"

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_THREAD_IPC)
#include "example_module_ipc.h"
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_MULTI_DEP)
#include "example_module_multi_dep.h"
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_GPIO)
#include "example_module_gpio.h"
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_UART)
#include "example_module_uart.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
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
static int  app_register_modules(void);
static void app_print_banner(void);

/* =============================================================================
 * Shell Commands
 * ============================================================================= */

#ifdef CONFIG_SHELL

static int cmd_app_status(const struct shell* shell, size_t argc, char** argv) {
    char version_str[VERSION_STRING_MAX_LEN];
    char info_str[VERSION_INFO_STRING_MAX_LEN];

    app_version_get_string(version_str, sizeof(version_str));
    app_version_get_info_string(info_str, sizeof(info_str));

    shell_print(shell, "Application Status:");
    shell_print(shell, "  Version: %s", version_str);
    shell_print(shell, "  Info: %s", info_str);
    shell_print(shell, "  State: %s", g_app.running ? "RUNNING" : "STOPPED");
    shell_print(shell, "  Uptime: %d ms", app_get_uptime());
    shell_print(shell, "  Heartbeats: %d", g_app.heartbeat_count);

    return 0;
}

static int cmd_app_modules(const struct shell* shell, size_t argc, char** argv) {
    shell_print(shell, "Registered Modules:");
    module_manager_dump_info();

    /* Get and print module stats */
    module_mgr_stats_t stats;
    module_manager_get_stats(&stats);
    shell_print(shell, "Module Statistics:");
    shell_print(shell, "  Total: %d", stats.total_modules);
    shell_print(shell, "  Active: %d", stats.active_modules);
    shell_print(shell, "  Errors: %d", stats.error_modules);

    return 0;
}

static int cmd_app_events(const struct shell* shell, size_t argc, char** argv) {
    uint32_t total_events, queue_depth, dropped_events;
    event_get_statistics(&total_events, &queue_depth, &dropped_events);

    shell_print(shell, "Event System Statistics:");
    shell_print(shell, "  Total Events: %d", total_events);
    shell_print(shell, "  Queue Depth: %d", queue_depth);
    shell_print(shell, "  Dropped: %d", dropped_events);

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
    shell_print(shell, "Memory Statistics:");
    shell_print(shell, "  Heap Size: %d bytes", sys_mem_get_heap_size());
    shell_print(shell, "  Free: %d bytes", sys_mem_get_free_size());
    shell_print(shell, "  Min Free: %d bytes", sys_mem_get_min_free_size());

    return 0;
}

static int cmd_app_log(const struct shell* shell, size_t argc, char** argv) {
#if !APP_CONFIG_ENABLE_LOG_DUMP
    shell_print(shell, "Log dump disabled (set APP_CONFIG_ENABLE_LOG_DUMP=1 in app_config.h)");
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
    shell_print(shell, "Available commands:");
    shell_print(shell, "  app status     - Show application status");
    shell_print(shell, "  app modules    - Show module information");
    shell_print(shell, "  app events     - Show event statistics");
    shell_print(shell, "  app memory     - Show memory statistics");
    shell_print(shell, "  app log [lvl]  - Dump log buffer");
    shell_print(shell, "  app help       - Show this help");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_app, CMD(status, cmd_app_status), CMD(modules, cmd_app_modules),
                               CMD(events, cmd_app_events), CMD(memory, cmd_app_memory), CMD(log, cmd_app_log),
                               CMD(help, cmd_app_help), SUBCMD_END);

SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);

#endif /* CONFIG_SHELL */

/* =============================================================================
 * Application API Implementation
 * ============================================================================= */

int app_init(const app_config_t* config) {
    LOG_INF("========================================");
    LOG_INF("Application Initializing...");
    LOG_INF("========================================");

    /* Print version information FIRST */
    app_version_print();

    memset(&g_app, 0, sizeof(g_app));

    /* Set configuration */
    if (config != NULL) {
        g_app.config = *config;
    } else {
        g_app.config.enable_logging = APP_CONFIG_ENABLE_LOGGING;
        g_app.config.enable_watchdog = APP_CONFIG_ENABLE_WATCHDOG;
        g_app.config.enable_shell = APP_CONFIG_ENABLE_SHELL;
        g_app.config.log_level = CONFIG_SYS_LOG_LEVEL;
    }

    /* Initialize logging */
#if APP_CONFIG_ENABLE_LOGGING
    sys_log_config_t log_config = {.default_level = (sys_log_level_t) g_app.config.log_level,
                                   .destinations = SYS_LOG_DEST_CONSOLE | SYS_LOG_DEST_MEMORY,
                                   .enable_timestamp = true,
                                   .enable_colors = true,
                                   .enable_module_name = true,
                                   .memory_buffer_size = CONFIG_SYS_MEMORY_POOL_SIZE};
    sys_log_init(&log_config);
    LOG_INF("Logging system initialized");
#endif

    /* Initialize memory management */
#if APP_CONFIG_ENABLE_MEMORY_MGR
    sys_mem_config_t mem_config = {.pool_sizes =
                                       {
                                           [SYS_MEM_POOL_GENERAL] = 8192,
                                           [SYS_MEM_POOL_EVENT] = 4096,
                                           [SYS_MEM_POOL_MODULE] = 4096,
                                       },
                                   .enable_tracking = true,
                                   .enable_defrag = false,
                                   .max_allocations = 256};
    sys_mem_init(&mem_config);
    LOG_INF("Memory system initialized");
#endif

    /* Initialize event system */
    event_system_init();
    LOG_INF("Event system initialized");

    /* Initialize event dispatcher (consumes queue; actual thread created in event_dispatcher_start)
     */
    dispatcher_config_t dispatcher_config = {.stack_size = CONFIG_EVENT_DISPATCHER_STACK_SIZE,
                                             .priority = CONFIG_EVENT_DISPATCHER_PRIORITY,
                                             .thread_name = "event_disp",
                                             .enable_stats = APP_CONFIG_ENABLE_STATS,
                                             .max_events_per_cycle = 100};
    if (event_dispatcher_init(&dispatcher_config) != EVENT_OK) {
        LOG_ERR("event_dispatcher_init failed");
        return APP_ERR_INIT;
    }
    LOG_INF("Event dispatcher initialized");

    /* Initialize timer service */
#if APP_CONFIG_ENABLE_TIMER_SVC
    sys_timer_init();
    LOG_INF("Timer service initialized");
#endif

    /* Initialize watchdog */
#if APP_CONFIG_ENABLE_WATCHDOG
    wdt_config_t wdt_config = {.mode = WDT_MODE_SOFTWARE,
                               .timeout_ms = APP_WATCHDOG_TIMEOUT_MS,
                               .feed_margin_ms = 1000,
                               .reset_on_expire = false};
    sys_wdt_init(&wdt_config);
    LOG_INF("Watchdog initialized");
#endif

    /* Initialize module manager */
    module_manager_init();
    LOG_INF("Module manager initialized");

    /* Register modules */
    app_register_modules();

    g_app.initialized = true;
    g_app.start_time = k_uptime_get_32();

    LOG_INF("Application initialization complete");
    return APP_OK;
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

    /* Start event system */
    event_system_start();

    /* Start event dispatcher thread (single consumer for event queue) */
    if (event_dispatcher_start() != EVENT_OK) {
        LOG_ERR("event_dispatcher_start failed");
        (void) event_system_stop();
        return APP_ERR_INIT;
    }
    LOG_INF("Event dispatcher started");

    /* Start module manager */
    module_manager_start();

    /* Start all registered modules */
    int started = module_manager_start_all();
    LOG_INF("Started %d modules", started);

    /* Start watchdog */
#if APP_CONFIG_ENABLE_WATCHDOG
    sys_wdt_start();
#endif

    /* Create heartbeat timer */
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
    module_manager_stop_all();

    /* Stop event dispatcher before event system */
    if (event_dispatcher_stop() != EVENT_OK) {
        LOG_ERR("event_dispatcher_stop failed");
    } else {
        LOG_INF("Event dispatcher stopped");
    }

    /* Stop event system */
    event_system_stop();

    /* Stop watchdog */
    sys_wdt_stop();

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

static int app_register_modules(void) {
    int      registered = 0;
    uint32_t module_id;

#if APP_CONFIG_ENABLE_MODULE_A
    example_module_a_config_t config_a = {.sample_rate_ms = 100, .buffer_size = 256, .enable_filtering = true};

    if (module_manager_register(example_module_a_get_interface(), &config_a, &module_id) == 0) {
        registered++;
        LOG_INF("Registered Module A (id=%d)", module_id);
    }
#endif

#if APP_CONFIG_ENABLE_MODULE_B
    example_module_b_config_t config_b = {.tx_buffer_size = 512, .rx_buffer_size = 512, .timeout_ms = 1000};

    if (module_manager_register(example_module_b_get_interface(), &config_b, &module_id) == 0) {
        registered++;
        LOG_INF("Registered Module B (id=%d)", module_id);
    }
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_MULTI_DEP)
    if (module_manager_register(example_module_multi_dep_get_interface(), NULL, &module_id) == 0) {
        registered++;
        LOG_INF("Registered example_module_multi_dep (id=%d)", module_id);
    }
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_THREAD_IPC)
    example_module_ipc_config_t config_ipc = {.reserved = 0};

    if (module_manager_register(example_module_ipc_get_interface(), &config_ipc, &module_id) == 0) {
        registered++;
        LOG_INF("Registered example_module_ipc (id=%d)", module_id);
    }
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_GPIO)
    example_module_gpio_config_t gpio_cfg = {
        .led_pin = "LED0",
        .button_pin = "SW0",
        .blink_interval_ms = 500,
        .enable_button = true,
    };

    if (module_manager_register(example_module_gpio_get_interface(), &gpio_cfg, &module_id) == 0) {
        registered++;
        LOG_INF("Registered example_module_gpio (id=%d)", module_id);
    }
#endif

#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_UART)
    example_module_uart_config_t uart_cfg = {0};

    if (module_manager_register(example_module_uart_get_interface(), &uart_cfg, &module_id) == 0) {
        registered++;
        LOG_INF("Registered example_module_uart (id=%d)", module_id);
    }
#endif

    LOG_INF("Total modules registered: %d", registered);
    return registered;
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
