/**
 * @file event_system_compat.c
 * @brief 事件系统兼容层实现
 */

#include "event_system_compat.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include "event_system.h"

LOG_MODULE_REGISTER(event_system_compat, CONFIG_SYS_LOG_LEVEL);

#if EVENT_COMPAT_USE_PRO
#include "event_system_pro.h"
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
        return -1;
    }
    LOG_INF("Event system PRO initialized");
    return 0;
#else
    event_status_t ret = event_system_init();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to init event_system: %d", ret);
        return -1;
    }
    LOG_INF("Event system (standard) initialized");
    return 0;
#endif
}

int event_compat_start(void) {
#if EVENT_COMPAT_USE_PRO
    int ret = event_system_pro_start();
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to start event_system_pro");
        return -1;
    }
#else
    event_status_t ret = event_system_start();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to start event_system");
        return -1;
    }
#endif
    return 0;
}

int event_compat_stop(void) {
#if EVENT_COMPAT_USE_PRO
    int ret = event_system_pro_stop();
    if (ret != EVENT_SYSTEM_PRO_OK) {
        LOG_ERR("Failed to stop event_system_pro");
        return -1;
    }
#else
    event_status_t ret = event_system_stop();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to stop event_system");
        return -1;
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
        return -1;
    }
    return 0;
#else
    int ret = event_system_shutdown();
    if (ret != EVENT_OK) {
        LOG_ERR("Failed to shutdown event_system: %d", ret);
        return -1;
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
    /* 标准版没有 reset_statistics，统计自动重置 */
#endif
}

/* =============================================================================
 * SYS_INIT 自动初始化
 * ============================================================================= */

#include <zephyr/init.h>
#include "app_config.h"

static int event_compat_auto_init(void) {
    if (event_compat_init(NULL) != 0) {
        LOG_ERR("event_compat_init failed");
        return -EIO;
    }

    if (event_compat_start() != 0) {
        LOG_ERR("event_compat_start failed");
        return -EIO;
    }

    LOG_INF("Event system initialized and started");
    return 0;
}

SYS_INIT(event_compat_auto_init, POST_KERNEL, APP_INIT_PRIO_EVENT_SYS);
