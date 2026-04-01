/**
 * @file event_dispatcher.c
 * @brief 事件分发器实现 (Event Dispatcher Implementation)
 *
 * 高性能事件分发器，支持优先级调度和统计功能。
 *
 * 主要功能：
 * - 事件分发线程管理
 * - 事件过滤
 * - 处理延迟统计
 * - 批处理支持
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

#include "event_dispatcher.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/time_units.h>
#include "event_queue.h"

LOG_MODULE_REGISTER(event_dispatcher, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义 (Internal Definitions)
 * ============================================================================= */

/** 默认栈大小（使用 Kconfig 配置） */
#define DEFAULT_STACK_SIZE              CONFIG_EVENT_DISPATCHER_STACK_SIZE

/** 默认优先级（使用 Kconfig 配置） */
#define DEFAULT_PRIORITY                CONFIG_EVENT_DISPATCHER_PRIORITY

/** 每个周期默认最大处理事件数 */
#define DEFAULT_MAX_EVENTS_CYCLE        100

/** 分发线程在 RUNNING 下从队列取事件的超时（便于响应 pause/stop，避免永久阻塞） */
#define DISPATCH_THREAD_MSGQ_TIMEOUT_MS 100

/* =============================================================================
 * 内部数据结构 (Internal Data Structures)
 * ============================================================================= */

/**
 * @brief 分发器控制块
 *
 * 包含分发器的所有状态和配置信息。
 */
typedef struct {
    dispatcher_state_t  state;                        /**< 分发器当前状态 */
    dispatcher_config_t config;                       /**< 分发器配置 */
    dispatcher_stats_t  stats;                        /**< 分发器统计信息 */
    struct k_thread     thread;                       /**< 分发器线程控制块 */
    K_KERNEL_STACK_MEMBER(stack, DEFAULT_STACK_SIZE); /**< 分发器线程栈 */
    event_filter_t filter;                            /**< 事件过滤函数 */
    void*          filter_user_data;                  /**< 过滤函数用户数据 */
    struct k_mutex lock;                              /**< 保护共享数据的互斥锁 */
    uint32_t       events_in_batch;                   /**< 当前批次处理的事件数 */
    uint64_t       last_event_time;                   /**< 上一个事件处理时间 */
    bool           thread_started;                    /**< 分发线程是否已创建并运行 */
} dispatcher_cb_t;

/* =============================================================================
 * 静态变量 (Static Variables)
 * ============================================================================= */

/** 全局分发器控制块实例 */
static dispatcher_cb_t g_dispatcher;

/** 全局事件队列指针（从事件系统获取） */
static struct k_msgq* g_event_queue;

/* =============================================================================
 * 前置声明 (Forward Declarations)
 * ============================================================================= */

/**
 * @brief 分发器线程入口函数
 */
static void dispatcher_thread_func(void* p1, void* p2, void* p3);

/**
 * @brief 处理单个事件
 * @param event 要处理的事件
 */
static void process_event(const event_t* event);

/**
 * @brief 计算事件处理延迟
 * @param event_timestamp 事件时间戳
 * @return 延迟时间（微秒）
 */
static uint32_t calculate_latency_us(uint64_t event_timestamp);

/* =============================================================================
 * 分发器控制 API (Dispatcher Control API)
 * ============================================================================= */

/**
 * @brief 初始化事件分发器
 *
 * @param config 分发器配置，NULL 使用默认配置
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 事件系统未初始化
 */
event_status_t event_dispatcher_init(const dispatcher_config_t* config) {
    LOG_INF("Initializing event dispatcher...");

    memset(&g_dispatcher, 0, sizeof(g_dispatcher));

    /* 设置默认配置或用户提供的配置 */
    if (config != NULL) {
        g_dispatcher.config = *config;
    } else {
        g_dispatcher.config.stack_size = DEFAULT_STACK_SIZE;
        g_dispatcher.config.priority = DEFAULT_PRIORITY;
        g_dispatcher.config.thread_name = "event_disp";
        g_dispatcher.config.enable_stats = true;
        g_dispatcher.config.max_events_per_cycle = DEFAULT_MAX_EVENTS_CYCLE;
    }

    /* 初始化同步原语 */
    k_mutex_init(&g_dispatcher.lock);

    g_dispatcher.state = DISPATCHER_STOPPED;
    g_dispatcher.last_event_time = k_uptime_get();
    g_dispatcher.thread_started = false;

    g_event_queue = event_system_get_queue();
    if (g_event_queue == NULL) {
        LOG_ERR("Call event_system_init() before event_dispatcher_init()");
        return EVENT_ERR_INVALID_ARG;
    }

    LOG_INF("Event dispatcher initialized");
    return EVENT_OK;
}

/**
 * @brief 启动分发器
 *
 * @return EVENT_OK 成功
 */
event_status_t event_dispatcher_start(void) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state == DISPATCHER_RUNNING) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_OK;
    }

    if (g_dispatcher.state == DISPATCHER_PAUSED) {
        g_dispatcher.state = DISPATCHER_RUNNING;
        k_mutex_unlock(&g_dispatcher.lock);
        LOG_INF("Event dispatcher resumed by start()");
        return EVENT_OK;
    }

    if (g_dispatcher.thread_started) {
        g_dispatcher.state = DISPATCHER_RUNNING;
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_OK;
    }

    g_dispatcher.state = DISPATCHER_RUNNING;

    /* 创建分发器线程 */
    k_thread_create(&g_dispatcher.thread, g_dispatcher.stack, g_dispatcher.config.stack_size, dispatcher_thread_func,
                    NULL, NULL, NULL, g_dispatcher.config.priority, 0, K_FOREVER);

    k_thread_name_set(&g_dispatcher.thread, g_dispatcher.config.thread_name);
    k_thread_start(&g_dispatcher.thread);
    g_dispatcher.thread_started = true;

    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher started");
    return EVENT_OK;
}

/**
 * @brief 停止分发器
 *
 * @return EVENT_OK 成功
 */
event_status_t event_dispatcher_stop(void) {
    bool should_join = false;

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state == DISPATCHER_STOPPED) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_OK;
    }

    g_dispatcher.state = DISPATCHER_STOPPED;
    should_join = g_dispatcher.thread_started;
    k_mutex_unlock(&g_dispatcher.lock);

    if (should_join && k_current_get() != &g_dispatcher.thread) {
        int jret = k_thread_join(&g_dispatcher.thread, K_MSEC(DISPATCH_THREAD_MSGQ_TIMEOUT_MS + 50));

        if (jret != 0) {
            LOG_WRN("Dispatcher thread join timeout/failed: %d", jret);
        }
    }

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.thread_started = false;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher stopped");
    return EVENT_OK;
}

/**
 * @brief 暂停分发器
 *
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 状态不正确
 */
event_status_t event_dispatcher_pause(void) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state != DISPATCHER_RUNNING) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_ERR_INVALID_ARG;
    }

    g_dispatcher.state = DISPATCHER_PAUSED;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher paused");
    return EVENT_OK;
}

/**
 * @brief 恢复分发器
 *
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 状态不正确
 */
event_status_t event_dispatcher_resume(void) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

    if (g_dispatcher.state != DISPATCHER_PAUSED) {
        k_mutex_unlock(&g_dispatcher.lock);
        return EVENT_ERR_INVALID_ARG;
    }

    g_dispatcher.state = DISPATCHER_RUNNING;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Event dispatcher resumed");
    return EVENT_OK;
}

/**
 * @brief 获取分发器状态
 *
 * @return 当前分发器状态
 */
dispatcher_state_t event_dispatcher_get_state(void) {
    dispatcher_state_t state;

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    state = g_dispatcher.state;
    k_mutex_unlock(&g_dispatcher.lock);

    return state;
}

/* =============================================================================
 * 事件处理 API (Event Processing API)
 * ============================================================================= */

/**
 * @brief 设置事件过滤器
 *
 * @param filter 过滤函数指针
 * @param user_data 用户数据
 */
void event_dispatcher_set_filter(event_filter_t filter, void* user_data) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.filter = filter;
    g_dispatcher.filter_user_data = user_data;
    k_mutex_unlock(&g_dispatcher.lock);
}

/**
 * @brief 清除事件过滤器
 */
void event_dispatcher_clear_filter(void) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.filter = NULL;
    g_dispatcher.filter_user_data = NULL;
    k_mutex_unlock(&g_dispatcher.lock);
}

/**
 * @brief 处理单个事件
 *
 * @param timeout 等待超时时间
 * @return EVENT_OK 成功，EVENT_ERR_INVALID_ARG 状态不正确，
 *         EVENT_ERR_QUEUE_EMPTY 队列为空
 */
event_status_t event_dispatcher_process_one(k_timeout_t timeout) {
    dispatcher_state_t state;
    event_filter_t     filter;
    void*              filter_user_data;

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    state = g_dispatcher.state;
    filter = g_dispatcher.filter;
    filter_user_data = g_dispatcher.filter_user_data;
    k_mutex_unlock(&g_dispatcher.lock);

    if (state != DISPATCHER_RUNNING) {
        return EVENT_ERR_INVALID_ARG;
    }

    event_t event;
    int     ret = k_msgq_get(g_event_queue, &event, timeout);
    if (ret != 0) {
        return EVENT_ERR_QUEUE_EMPTY;
    }

    /* 应用过滤器（如果已设置） */
    if (filter != NULL) {
        if (!filter(&event, filter_user_data)) {
            if (event.is_dynamic && event.data != NULL) {
                k_free(event.data);
            }
            if (g_dispatcher.config.enable_stats) {
                k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
                g_dispatcher.stats.events_dropped++;
                k_mutex_unlock(&g_dispatcher.lock);
            }
            return EVENT_OK;
        }
    }

    process_event(&event);

    /* 释放动态数据 */
    if (event.is_dynamic && event.data != NULL) {
        k_free(event.data);
    }

    return EVENT_OK;
}

/**
 * @brief 处理所有待处理事件
 *
 * @param max_events 最大处理事件数，0 使用配置默认值
 * @return 已处理的事件数量
 */
uint32_t event_dispatcher_process_all(uint32_t max_events) {
    if (max_events == 0) {
        max_events = g_dispatcher.config.max_events_per_cycle;
    }

    uint32_t processed = 0;
    while (processed < max_events) {
        if (event_dispatcher_process_one(K_NO_WAIT) != EVENT_OK) {
            break; /* 无更多事件 */
        }
        processed++;
    }

    return processed;
}

/* =============================================================================
 * 统计 API (Statistics API)
 * ============================================================================= */

/**
 * @brief 获取分发器统计信息
 *
 * @param stats 输出：统计信息结构指针
 */
void event_dispatcher_get_stats(dispatcher_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    *stats = g_dispatcher.stats;
    k_mutex_unlock(&g_dispatcher.lock);
}

/**
 * @brief 重置分发器统计信息
 */
void event_dispatcher_reset_stats(void) {
    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    memset(&g_dispatcher.stats, 0, sizeof(g_dispatcher.stats));
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_DBG("Dispatcher statistics reset");
}

/**
 * @brief 获取当前事件延迟
 *
 * @return 延迟时间（微秒）
 */
uint32_t event_dispatcher_get_current_latency(void) {
    uint64_t last_event_time;

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    last_event_time = g_dispatcher.last_event_time;
    k_mutex_unlock(&g_dispatcher.lock);

    return calculate_latency_us(last_event_time);
}

/* =============================================================================
 * 内部函数 (Internal Functions)
 * ============================================================================= */

/**
 * @brief 分发器线程入口函数
 *
 * 主循环：RUNNING 时带超时取队事件；PAUSED 时休眠不消费；STOPPED 时退出。
 */
static void dispatcher_thread_func(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Dispatcher thread running");

    while (1) {
        dispatcher_state_t st;

        k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
        st = g_dispatcher.state;
        k_mutex_unlock(&g_dispatcher.lock);

        if (st == DISPATCHER_STOPPED) {
            break;
        }

        if (st == DISPATCHER_PAUSED) {
            k_msleep(10);
            continue;
        }

        (void) event_dispatcher_process_one(K_MSEC(DISPATCH_THREAD_MSGQ_TIMEOUT_MS));
    }

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.thread_started = false;
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_INF("Dispatcher thread exiting");
}

/**
 * @brief 处理单个事件
 *
 * 调用 event_notify_subscribers 分发事件，并更新统计信息。
 *
 * @param event 要处理的事件
 */
static void process_event(const event_t* event) {
    if (event == NULL) {
        return;
    }

    uint64_t start_time = k_cycle_get_64();

    event_status_t status = event_notify_subscribers(event);

    uint64_t end_time = k_cycle_get_64();
    uint32_t latency_us = (uint32_t) ((end_time - start_time) * 1000000ULL / sys_clock_hw_cycles_per_sec());

    /* 更新统计信息 */
    if (g_dispatcher.config.enable_stats) {
        k_mutex_lock(&g_dispatcher.lock, K_FOREVER);

        g_dispatcher.stats.events_processed++;

        if (status != EVENT_OK) {
            g_dispatcher.stats.processing_errors++;
        }

        if (latency_us > g_dispatcher.stats.max_latency_us) {
            g_dispatcher.stats.max_latency_us = latency_us;
        }

        /* 运行平均延迟 */
        uint64_t total =
            (uint64_t) g_dispatcher.stats.avg_latency_us * (g_dispatcher.stats.events_processed - 1) + latency_us;
        g_dispatcher.stats.avg_latency_us = (uint32_t) (total / g_dispatcher.stats.events_processed);

        k_mutex_unlock(&g_dispatcher.lock);
    }

    k_mutex_lock(&g_dispatcher.lock, K_FOREVER);
    g_dispatcher.last_event_time = k_uptime_get();
    k_mutex_unlock(&g_dispatcher.lock);

    LOG_DBG("Processed event type=%d, latency=%dus", event->type, latency_us);
}

/**
 * @brief 计算延迟（微秒）
 *
 * @param event_timestamp 事件时间戳
 * @return 延迟时间（微秒）
 */
static uint32_t calculate_latency_us(uint64_t event_timestamp) {
    uint64_t now = k_uptime_get();
    uint64_t delta_ms = now - event_timestamp;
    return (uint32_t) (delta_ms * 1000); /* 毫秒转微秒 */
}
