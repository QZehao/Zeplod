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

 *

 */

#include <zephyr/init.h>

#include <zephyr/logging/log.h>

#include "app_config.h"

#include "event_dispatcher.h"

#include "event_system.h"

#include "event_system_compat.h"

LOG_MODULE_REGISTER(event_dispatcher_autoinit, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================

 * SYS_INIT 自动初始化

 * ============================================================================= */

#if !EVENT_COMPAT_USE_PRO

static int event_dispatcher_auto_init(void) {

    bool local_system_init = false;

    bool local_system_start = false;

    struct k_msgq* queue = event_system_get_queue();

    if (queue == NULL) {
        if (event_system_init() != EVENT_OK) {
            LOG_ERR("event_system_init failed");
            return -EIO;
        }
        local_system_init = true;
        LOG_INF("Event system auto-initialized by dispatcher");
    }

    if (!event_system_is_running()) {
        if (event_system_start() != EVENT_OK) {
            LOG_ERR("event_system_start failed");
            if (local_system_init) {
                (void) event_system_shutdown();
            }
            return -EIO;
        }
        local_system_start = true;
    }

    dispatcher_config_t dispatcher_config = {.stack_size = CONFIG_EVENT_DISPATCHER_STACK_SIZE,
                                             .priority = CONFIG_EVENT_DISPATCHER_PRIORITY,
                                             .thread_name = "event_disp",
                                             .enable_stats = APP_CONFIG_ENABLE_STATS,
                                             .max_events_per_cycle = CONFIG_EVENT_DISPATCHER_MAX_EVENTS_PER_CYCLE};
    if (event_dispatcher_init(&dispatcher_config) != EVENT_OK) {
        LOG_ERR("event_dispatcher_init failed");
        if (local_system_start) {
            (void) event_system_stop();
        }
        if (local_system_init) {

            (void) event_system_shutdown();
        }

        return -EIO;
    }

    if (event_dispatcher_start() != EVENT_OK) {
        LOG_ERR("event_dispatcher_start failed");
        (void) event_dispatcher_stop();
        if (local_system_start) {

            (void) event_system_stop();
        }
        if (local_system_init) {

            (void) event_system_shutdown();
        }

        return -EIO;
    }

    LOG_INF("Event dispatcher initialized and started");

    return 0;
}

SYS_INIT(event_dispatcher_auto_init, POST_KERNEL, APP_INIT_PRIO_DISPATCHER);

#endif /* !EVENT_COMPAT_USE_PRO */
