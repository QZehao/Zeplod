/**
 * @file app_main.c
 * @brief 应用主入口实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 * 2026-05-28       1.1            zeh            Shell 命令迁至 app_shell.c（P4.3）
 *
 */

#include "app_main.h"
#include "app_banner.h"
#include "app_kv.h"
#include "event_dispatcher.h"
#include "event_system.h"
#include "event_system_compat.h"
#include "module_manager_compat.h"
#include "sys_log.h"
#include "sys_timer.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app_main, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    app_config_t config;
    bool         initialized;
    atomic_t     running;
    uint32_t     start_time;
    atomic_t     heartbeat_count;
} app_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static app_cb_t g_app;

#if APP_CONFIG_ENABLE_TIMER_SVC && (APP_HEARTBEAT_INTERVAL_MS > 0)
static sys_timer_handle_t g_heartbeat_timer;
#endif

/* =============================================================================
 * 前置声明
 * ============================================================================= */

static void app_heartbeat_timer_callback(sys_timer_handle_t timer, void* user_data);
static void app_heartbeat_timer_teardown(void);
static void app_print_banner(void);
static void app_apply_runtime_config(void);

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

/* Shell 命令见 app_shell.c */

/* =============================================================================
 * 自动初始化步骤 (SYS_INIT, POST_KERNEL; 通过 APP_INIT_PRIO_* 控制顺序)
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

static void app_apply_runtime_config(void) {
#if APP_CONFIG_ENABLE_LOGGING
    if (g_app.config.enable_logging) {
        sys_log_level_t lv = (sys_log_level_t) g_app.config.log_level;

        if (lv >= SYS_LOG_LEVEL_MAX) {
            lv = SYS_LOG_LEVEL_DBG;
        }
        sys_log_set_level(NULL, lv);
    }
#endif
}

static int app_init_apply_cb(void) {
    app_version_print();

    memset(&g_app, 0, sizeof(g_app));
    app_apply_config(NULL);

    return 0;
}

#if APP_CONFIG_ENABLE_APP_KV
static int app_init_kv_step(void) {
    app_kv_init();
    (void) app_kv_set("build.target", BUILD_TARGET);
    return 0;
}
#endif

static int app_init_finalize(void) {
    g_app.initialized = true;
    app_apply_runtime_config();
    LOG_DBG("Application initialization complete");
    return 0;
}

/* =============================================================================
 * 应用 API 实现
 * ============================================================================= */

int app_init(const app_config_t* config) {
    if (!g_app.initialized) {
        return APP_ERR_INIT;
    }

    /* 子系统与模块由 SYS_INIT(POST_KERNEL) 在 main 之前完成；此处仅应用可选运行时配置。 */
    if (config != NULL) {
        app_apply_config(config);
        app_apply_runtime_config();
    }

    return APP_OK;
}

int app_start(void) {
    if (!g_app.initialized) {
        LOG_ERR("Application not initialized");
        return APP_ERR_INIT;
    }

    if (atomic_get(&g_app.running)) {
        LOG_WRN("Application already running");
        return APP_OK;
    }

    LOG_DBG("Starting application...");

    /* 事件系统/分发器已由 SYS_INIT 启动；module_compat SYS_INIT 仅启动管理器本身。 */

    /* 启动各已注册业务模块（INITIALIZED/STOPPED → RUNNING） */
    int started = module_compat_start_all();
    LOG_DBG("Started %d modules", started);

#if APP_CONFIG_ENABLE_WATCHDOG
    if (g_app.config.enable_watchdog) {
        int wdt_rc = sys_wdt_start();
        if (wdt_rc != 0) {
            LOG_ERR("sys_wdt_start failed: %d", wdt_rc);
            return APP_ERR_INIT;
        }
    }
#endif

#if APP_CONFIG_ENABLE_TIMER_SVC && (APP_HEARTBEAT_INTERVAL_MS > 0)
    if (g_heartbeat_timer == NULL) {
        sys_timer_config_t heartbeat_config = {.mode = SYS_TIMER_PERIODIC,
                                               .delay_ms = APP_HEARTBEAT_INTERVAL_MS,
                                               .period_ms = APP_HEARTBEAT_INTERVAL_MS,
                                               .callback = app_heartbeat_timer_callback,
                                               .user_data = NULL,
                                               .name = "heartbeat",
                                               .priority = APP_PRIORITY_MODULE_LOW};
        g_heartbeat_timer = sys_timer_create(&heartbeat_config);
        if (g_heartbeat_timer == NULL) {
            LOG_ERR("Heartbeat timer create failed");
            return APP_ERR_INIT;
        }
        int tmr_rc = sys_timer_start(g_heartbeat_timer);
        if (tmr_rc != 0) {
            LOG_ERR("sys_timer_start(heartbeat) failed: %d", tmr_rc);
            app_heartbeat_timer_teardown();
            return APP_ERR_INIT;
        }
    }
#endif

    atomic_set(&g_app.running, 1);
    g_app.start_time = k_uptime_get_32();

    app_print_banner();
    LOG_INF("Application ready (modules=%d)", started);

    return APP_OK;
}

int app_stop(void) {
    if (!atomic_get(&g_app.running)) {
        return APP_OK;
    }

    LOG_INF("Stopping application...");

#if APP_CONFIG_ENABLE_TIMER_SVC && (APP_HEARTBEAT_INTERVAL_MS > 0)
    app_heartbeat_timer_teardown();
#endif

    atomic_set(&g_app.running, 0);

    /* 停止所有模块 */
    module_compat_stop_all();

    /* 在事件系统之前停止事件分发器 */
    if (event_dispatcher_stop() != EVENT_OK) {
        LOG_ERR("event_dispatcher_stop failed");
    } else {
        LOG_INF("Event dispatcher stopped");
    }

    /* 停止事件系统 */
    if (event_compat_stop() != 0) {
        LOG_ERR("event_compat_stop failed");
    }

#if APP_CONFIG_ENABLE_WATCHDOG
    if (g_app.config.enable_watchdog) {
        sys_wdt_stop();
    }
#endif

    LOG_INF("Application stopped");
    return APP_OK;
}

uint32_t app_get_uptime(void) {
    if (!g_app.initialized || !atomic_get(&g_app.running)) {
        return 0;
    }
    return k_uptime_get_32() - g_app.start_time;
}

bool app_is_running(void) {
    return atomic_get(&g_app.running) != 0;
}

uint32_t app_get_heartbeat_count(void) {
    return (uint32_t) atomic_get(&g_app.heartbeat_count);
}

/* =============================================================================
 * 内部函数
 * ============================================================================= */

#if APP_CONFIG_ENABLE_TIMER_SVC && (APP_HEARTBEAT_INTERVAL_MS > 0)
static void app_heartbeat_timer_teardown(void) {
    if (g_heartbeat_timer == NULL) {
        return;
    }
    (void) sys_timer_stop(g_heartbeat_timer);
    (void) sys_timer_delete(g_heartbeat_timer);
    g_heartbeat_timer = NULL;
}
#endif

static void app_heartbeat_timer_callback(sys_timer_handle_t timer, void* user_data) {
    ARG_UNUSED(timer);
    ARG_UNUSED(user_data);

    if (!atomic_get(&g_app.running)) {
        return;
    }

    uint32_t count = (uint32_t) atomic_inc(&g_app.heartbeat_count);

    /* 喂看门狗 */
#if APP_CONFIG_ENABLE_WATCHDOG
    sys_wdt_feed();
#endif

    /* 记录周期性状态 */
    if (count % 10U == 0U) {
        LOG_DBG("Heartbeat: %u, Uptime: %ums", count, app_get_uptime());
    }
}

static void app_print_banner(void) {
#if APP_CONFIG_ENABLE_BANNER
    printk(APP_BANNER_LOGO);
    printk(APP_BANNER_INFO, "Version:", APP_VERSION_STRING, "Target:", BUILD_TARGET, "Git:", GIT_BRANCH,
           GIT_COMMIT_HASH, "Build:", BUILD_TIMESTAMP, "Ver code:", (unsigned int) APP_VERSION_CODE,
           "Build type:", BUILD_TYPE);
    printk("\r\n");
#endif
}

/* =============================================================================
 * 主入口
 * ============================================================================= */

int main(void) {
    LOG_DBG("FW_MARKER: %s | %s", GIT_COMMIT_HASH, BUILD_TIMESTAMP);
    /* 初始化应用 */
    if (app_init(NULL) != APP_OK) {
        LOG_ERR("Application initialization failed");
        return -1;
    }

    /* 启动应用 */
    if (app_start() != APP_OK) {
        LOG_ERR("Application start failed");
        return -1;
    }

    /* 主循环 - 事件驱动设计中，此处大部分时间空闲 */
    while (1) {
        /* 睡眠以节省功耗 */
        k_msleep(1000);

        /* 如需可在此处添加主循环任务 */
    }

    /* 不应执行到此处 */
    return 0;
}
