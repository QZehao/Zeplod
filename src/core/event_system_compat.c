/**
 * @file event_system_compat.c
 * @brief 标准事件系统应用适配层与自动初始化
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-06
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-09       1.0            zeh            初始版本
 * 2026-06-06       1.1            zeh            移除商业事件系统适配，仅保留标准实现
 */

#include "event_system_compat.h"
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include "app_config.h"

LOG_MODULE_REGISTER(event_system_compat, CONFIG_SYS_LOG_LEVEL);

static int event_status_to_errno(event_status_t status) {
    switch (status) {
    case EVENT_OK:
        return 0;
    case EVENT_ERR_NO_MEM:
        return -ENOMEM;
    case EVENT_ERR_INVALID_ARG:
        return -EINVAL;
    case EVENT_ERR_TIMEOUT:
        return -ETIMEDOUT;
    case EVENT_ERR_NOT_RUNNING:
        return -ECANCELED;
    default:
        return -EIO;
    }
}

int event_compat_init(const event_compat_config_t* config) {
    ARG_UNUSED(config);

    event_status_t ret = event_system_init();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to init event_system: %d", ret);
        return event_status_to_errno(ret);
    }

    LOG_DBG("Event system initialized");
    return 0;
}

int event_compat_start(void) {
    event_status_t ret = event_system_start();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to start event_system: %d", ret);
        return event_status_to_errno(ret);
    }
    return 0;
}

int event_compat_stop(void) {
    event_status_t ret = event_system_stop();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to stop event_system: %d", ret);
        return event_status_to_errno(ret);
    }
    return 0;
}

bool event_compat_is_running(void) {
    return event_system_is_running();
}

int event_compat_shutdown(void) {
    event_status_t ret = event_system_shutdown();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to shutdown event_system: %d", ret);
        return event_status_to_errno(ret);
    }
    return 0;
}

void event_compat_get_statistics(event_compat_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    event_get_statistics(&stats->total_events, &stats->queue_depth, &stats->dropped_events);
}

void event_compat_reset_statistics(void) {
    event_system_reset_statistics();
}

/* =============================================================================
 * SYS_INIT 自动初始化
 *
 * 本文件在 APP_INIT_PRIO_EVENT_SYS 完成事件系统初始化。启用分发器自动初始化时，
 * start 延后到 event_dispatcher_autoinit.c，避免消费者就绪前已有事件入队。
 * ============================================================================= */

#if IS_ENABLED(CONFIG_EVENT_SYSTEM_AUTOINIT)

static int event_compat_auto_init(void) {
    if (event_compat_init(NULL) != 0) {
        LOG_ERR("event_compat_init failed");
        return -EIO;
    }

#if IS_ENABLED(CONFIG_EVENT_DISPATCHER_AUTOINIT)
    LOG_DBG("Event system initialized (start deferred to dispatcher autoinit)");
    return 0;
#else
    if (event_compat_start() != 0) {
        LOG_ERR("event_compat_start failed");
        (void) event_compat_shutdown();
        return -EIO;
    }

    LOG_DBG("Event system initialized and started");
    return 0;
#endif
}

SYS_INIT(event_compat_auto_init, POST_KERNEL, APP_INIT_PRIO_EVENT_SYS);

#endif /* CONFIG_EVENT_SYSTEM_AUTOINIT */
