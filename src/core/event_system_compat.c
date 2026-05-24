/**
 * @file event_system_compat.c
 * @brief 事件系统兼容层实现（标准版与商业版统一入口、SYS_INIT）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-09       1.0            zeh            初始版本
 * 2026-05-09       1.0            zeh            完善文件头与修改日志
 *
 */

#include "event_system_compat.h"
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include "event_system.h"

LOG_MODULE_REGISTER(event_system_compat, CONFIG_SYS_LOG_LEVEL);

#if EVENT_COMPAT_USE_PRO
#include "event_system_pro.h"
#endif

#if !EVENT_COMPAT_USE_PRO
static inline int event_status_to_errno(event_status_t status) {
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
#endif

int event_compat_init(const event_compat_config_t* config) {
#if EVENT_COMPAT_USE_PRO
    event_system_pro_config_t pro_config = {0};

    if (config != NULL) {
        pro_config.high_priority_queue_size = config->high_priority_queue_size;
        pro_config.normal_priority_queue_size = config->normal_priority_queue_size;
        pro_config.low_priority_queue_size = config->low_priority_queue_size;
        pro_config.enable_playback = config->enable_playback;
        pro_config.enable_statistics = config->enable_statistics;
        pro_config.enable_rate_limit = config->enable_rate_limit;
        pro_config.enable_batch = config->enable_batch;
        pro_config.enable_persist = config->enable_persist;
        pro_config.enable_profiling = config->enable_profiling;
        pro_config.enable_security = config->enable_security;
    } else {
        pro_config.high_priority_queue_size = 64;
        pro_config.normal_priority_queue_size = 128;
        pro_config.low_priority_queue_size = 256;
        pro_config.enable_playback = true;
        pro_config.enable_statistics = true;
    }

    int ret = event_system_pro_init(&pro_config);
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to init event_system_pro: %d", ret);
        return -EIO;
    }
    LOG_DBG("Event system PRO initialized");
    return 0;
#else
    /* SIL-2: 检测配置请求了专业版功能，但在标准版下被静默忽略 */
    if (config != NULL && (config->enable_playback || config->enable_persist || config->enable_profiling ||
                           config->enable_security || config->enable_rate_limit || config->enable_batch)) {
        LOG_WRN("Config requests pro features, falling back to standard implementation");
    }
    event_status_t ret = event_system_init();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to init event_system: %d", ret);
        return event_status_to_errno(ret);
    }
    LOG_DBG("Event system (standard) initialized");
    return 0;
#endif
}

int event_compat_start(void) {
#if EVENT_COMPAT_USE_PRO
    int ret = event_system_pro_start();
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to start event_system_pro");
        return -EIO;
    }
#else
    event_status_t ret = event_system_start();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to start event_system");
        return event_status_to_errno(ret);
    }
#endif
    return 0;
}

int event_compat_stop(void) {
#if EVENT_COMPAT_USE_PRO
    int ret = event_system_pro_stop();
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to stop event_system_pro");
        return -EIO;
    }
#else
    event_status_t ret = event_system_stop();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to stop event_system");
        return event_status_to_errno(ret);
    }
#endif
    return 0;
}

bool event_compat_is_running(void) {
#if EVENT_COMPAT_USE_PRO
    return event_system_pro_is_running();
#else
    return event_system_is_running();
#endif
}

int event_compat_shutdown(void) {
#if EVENT_COMPAT_USE_PRO
    int ret = event_system_pro_shutdown();
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to shutdown event_system_pro");
        return -EIO;
    }
    return 0;
#else
    event_status_t ret = event_system_shutdown();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to shutdown event_system: %d", ret);
        return event_status_to_errno(ret);
    }
    return 0;
#endif
}

void event_compat_get_statistics(event_compat_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));

#if EVENT_COMPAT_USE_PRO
    event_system_pro_stats_t pro_stats = {0};
    event_system_pro_get_statistics(&pro_stats);

    stats->total_events = pro_stats.total_events;
    stats->queue_depth = pro_stats.queue_depth;
    stats->dropped_events = pro_stats.dropped_events;
    stats->high_priority_processed = pro_stats.high_priority_processed;
    stats->batch_operations = pro_stats.batch_operations;
    stats->rate_limited_events = pro_stats.rate_limited_events;
#else
    event_get_statistics(&stats->total_events, &stats->queue_depth, &stats->dropped_events);
#endif
}

void event_compat_reset_statistics(void) {
#if EVENT_COMPAT_USE_PRO
    event_system_pro_reset_statistics();
#else
    /* SIL-2: 标准版实现统计重置，避免统计值累积溢出（IMP-5 修复） */
    event_system_reset_statistics();
#endif
}

/* =============================================================================
 * SYS_INIT 自动初始化
 *
 * 标准版：本文件 @ APP_INIT_PRIO_EVENT_SYS 完成 event_system init；
 * 若同时启用 CONFIG_EVENT_DISPATCHER_AUTOINIT，则 start 推迟到
 * event_dispatcher_autoinit.c（@ APP_INIT_PRIO_DISPATCHER），避免无消费者时入队。
 * 默认勿再手动调用 event_dispatcher_init/start，除非已禁用 autoinit 对象文件。
 * ============================================================================= */

#include <zephyr/init.h>
#include "app_config.h"

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
