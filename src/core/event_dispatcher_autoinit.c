/**
 * @file event_dispatcher_autoinit.c
 * @brief 事件分发器自动初始化
 *
 * 为事件分发器提供 SYS_INIT 自动初始化机制。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-10       1.0            zeh            初始版本
 * 2026-05-19       1.1            zeh            已 init 但未 start 时补调 event_system_start
 * 2026-05-20       1.2            zeh            失败回滚仅针对本函数创建的状态
 * 2026-05-23       1.3            zeh            回滚按 local_* 标志，避免未 init 时 stop
 * 2026-05-23       1.4            zeh            回滚用 deinit；移除未使用的 local_dispatcher_start
 *
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zeplod/app_config.h>
#include <zeplod/event_dispatcher.h>
#include <zeplod/event_system.h>
#include <zeplod/event_system_compat.h>

LOG_MODULE_REGISTER(event_dispatcher_autoinit, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * SYS_INIT 自动初始化
 * ============================================================================= */

#if IS_ENABLED(CONFIG_EVENT_DISPATCHER_AUTOINIT)

/**
 * @brief 按与 init 相反的顺序回滚本函数创建的状态
 *
 * @param local_system_init  本函数是否调用了 event_system_init()
 * @param local_system_start 本函数是否调用了 event_system_start()
 * @param local_dispatcher_init 本函数是否成功调用了 event_dispatcher_init()
 */
static void event_dispatcher_autoinit_rollback(bool local_system_init, bool local_system_start,
                                               bool local_dispatcher_init) {
    if (local_dispatcher_init) {
        event_status_t dret = event_dispatcher_deinit();
        if (dret != EVENT_OK) {
            LOG_ERR("event_dispatcher_deinit failed (%d) during rollback; aborting to avoid use-after-free", dret);
            return;
        }
    }

    if (local_system_start) {
        (void) event_system_stop();
    }

    if (local_system_init) {
        (void) event_system_shutdown();
    }
}

static int event_dispatcher_auto_init(void) {
    bool local_system_init = false;
    bool local_system_start = false;
    bool local_dispatcher_init = false;

    struct k_msgq* queue = event_system_get_queue();

    if (queue == NULL) {
        if (event_system_init() != EVENT_OK) {
            LOG_ERR("event_system_init failed");
            return -EIO;
        }
        local_system_init = true;
        LOG_DBG("Event system auto-initialized by dispatcher");
    }

    if (!event_system_is_running()) {
        if (event_system_start() != EVENT_OK) {
            LOG_ERR("event_system_start failed");
            event_dispatcher_autoinit_rollback(local_system_init, false, false);
            return -EIO;
        }
        local_system_start = true;
    }

    if (!event_dispatcher_is_initialized()) {
        dispatcher_config_t dispatcher_config = {.stack_size = CONFIG_EVENT_DISPATCHER_STACK_SIZE,
                                                 .priority = CONFIG_EVENT_DISPATCHER_PRIORITY,
                                                 .thread_name = "event_disp",
                                                 .enable_stats = APP_CONFIG_ENABLE_STATS,
                                                 .max_events_per_cycle = CONFIG_EVENT_DISPATCHER_MAX_EVENTS_PER_CYCLE};
        if (event_dispatcher_init(&dispatcher_config) != EVENT_OK) {
            LOG_ERR("event_dispatcher_init failed");
            event_dispatcher_autoinit_rollback(local_system_init, local_system_start, false);
            return -EIO;
        }
        local_dispatcher_init = true;
    } else {
        LOG_DBG("Dispatcher already initialized, skipping auto-init");
    }

    if (event_dispatcher_start() != EVENT_OK) {
        LOG_ERR("event_dispatcher_start failed");
        event_dispatcher_autoinit_rollback(local_system_init, local_system_start, local_dispatcher_init);
        return -EIO;
    }

    LOG_DBG("Event dispatcher initialized and started");

    return 0;
}

SYS_INIT(event_dispatcher_auto_init, POST_KERNEL, APP_INIT_PRIO_DISPATCHER);

#endif /* CONFIG_EVENT_DISPATCHER_AUTOINIT */
