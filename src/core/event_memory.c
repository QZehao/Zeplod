/**
 * @file event_memory.c
 * @brief 事件系统内存管理模块实现 (Event Memory Management Implementation)
 *
 * 实现 Slab 池定义和内存管理函数。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-15
 *
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-04-15       1.0            zeh            初始版本
 */

#include "event_memory.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(event_memory, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Slab 池定义 (Slab Pool Definitions)
 * 使用 K_MEM_SLAB_DEFINE 定义 Slab 池
 * ============================================================================= */

#if EVENT_SLAB_ENABLED

#if EVENT_SLAB_CRITICAL_AVAILABLE
/** CRITICAL 优先级事件 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_critical, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_CRITICAL_COUNT, sizeof(void*));
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
/** HIGH 优先级事件 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_high, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_HIGH_COUNT, sizeof(void*));
#endif

/** NORMAL/LOW 优先级事件 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_normal, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_NORMAL_COUNT, sizeof(void*));

#if EVENT_SLAB_LARGE_AVAILABLE

#if EVENT_SLAB_256_AVAILABLE
/** 256 字节数据块 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_data_256, 256,
                  CONFIG_EVENT_SLAB_LARGE_256_COUNT, sizeof(void*));
#endif

#if EVENT_SLAB_1K_AVAILABLE
/** 1KB 数据块 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_data_1k, 1024,
                  CONFIG_EVENT_SLAB_LARGE_1K_COUNT, sizeof(void*));
#endif

#if EVENT_SLAB_4K_AVAILABLE
/** 4KB 数据块 Slab 池定义 */
K_MEM_SLAB_DEFINE(event_slab_data_4k, 4096,
                  CONFIG_EVENT_SLAB_LARGE_4K_COUNT, sizeof(void*));
#endif

#endif /* EVENT_SLAB_LARGE_AVAILABLE */

#endif /* EVENT_SLAB_ENABLED */

/* =============================================================================
 * 统计计数器 (Statistics Counters)
 * 条件编译：CONFIG_EVENT_RUNTIME_STATUS
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)
/** 回退到 k_malloc 的计数器 */
static atomic_t g_fallback_count = ATOMIC_INIT(0);
#endif

/* =============================================================================
 * Slab 耗尽回调实现 (Slab Exhausted Callback Implementation)
 * 条件编译：CONFIG_EVENT_SLAB_EXHAUSTED_CB
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_EXHAUSTED_CB) && (CONFIG_EVENT_SLAB_EXHAUSTED_CB == 1)

/** Slab 耗尽回调函数指针 */
static event_slab_exhausted_cb_t g_slab_exhausted_cb = NULL;

void event_register_slab_exhausted_cb(event_slab_exhausted_cb_t cb)
{
    g_slab_exhausted_cb = cb;
}

/**
 * @brief 通知 Slab 耗尽（内部函数）
 *
 * @param priority 触发耗尽的优先级
 * @param slab_name Slab 名称
 */
static inline void notify_slab_exhausted(event_priority_t priority, const char* slab_name)
{
    if (g_slab_exhausted_cb != NULL) {
        g_slab_exhausted_cb(priority, slab_name);
    }
}

#else /* !CONFIG_EVENT_SLAB_EXHAUSTED_CB */

/** 空实现的耗尽通知（编译优化后会被移除） */
static inline void notify_slab_exhausted(event_priority_t priority, const char* slab_name)
{
    (void)priority;
    (void)slab_name;
}

#endif /* CONFIG_EVENT_SLAB_EXHAUSTED_CB */

/* =============================================================================
 * 内部分配函数实现 (Internal Allocation Functions Implementation)
 * ============================================================================= */

struct k_mem_slab* event_memory_select_event_slab(event_priority_t priority)
{
    switch (priority) {
#if EVENT_SLAB_CRITICAL_AVAILABLE
    case EVENT_PRIORITY_CRITICAL:
        return &event_slab_critical;
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
    case EVENT_PRIORITY_HIGH:
        return &event_slab_high;
#endif

    case EVENT_PRIORITY_NORMAL:
    case EVENT_PRIORITY_LOW:
    default:
        /* SIL-2: LOW 与 NORMAL 共用同一 slab 池是有意设计，
         * 两者在典型场景下数量最多，合并可减少内存碎片 */
        return &event_slab_normal;
    }
}

struct k_mem_slab* event_memory_select_data_slab(size_t data_len)
{
    if (data_len == 0) {
        return NULL;
    }

#if EVENT_SLAB_256_AVAILABLE
    if (data_len <= 256) {
        return &event_slab_data_256;
    }
#endif

#if EVENT_SLAB_1K_AVAILABLE
    if (data_len <= 1024) {
        return &event_slab_data_1k;
    }
#endif

#if EVENT_SLAB_4K_AVAILABLE
    if (data_len <= 4096) {
        return &event_slab_data_4k;
    }
#endif

    /* 数据过大，需要使用 k_malloc */
    return NULL;
}

struct k_mem_slab* event_memory_select_data_slab_with_fallback(size_t data_len)
{
    if (data_len == 0) {
        return NULL;
    }

    /* MED-NEW-3: 级联策略 —— 首选最优大小，若满则尝试更大的池 */
#if EVENT_SLAB_256_AVAILABLE
    if (data_len <= 256 && k_mem_slab_num_free_get(&event_slab_data_256) > 0) {
        return &event_slab_data_256;
    }
#endif

#if EVENT_SLAB_1K_AVAILABLE
    if (data_len <= 1024 && k_mem_slab_num_free_get(&event_slab_data_1k) > 0) {
        return &event_slab_data_1k;
    }
#endif

#if EVENT_SLAB_4K_AVAILABLE
    if (data_len <= 4096 && k_mem_slab_num_free_get(&event_slab_data_4k) > 0) {
        return &event_slab_data_4k;
    }
#endif

    return NULL;
}

/* =============================================================================
 * 回退计数 API (Fallback Count API)
 * ============================================================================= */

void event_memory_inc_fallback_count(void)
{
#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)
    atomic_inc(&g_fallback_count);
#else
    /* 无统计功能，空操作 */
#endif
}

/* =============================================================================
 * 运行时状态 API 实现 (Runtime Status API Implementation)
 * 条件编译：CONFIG_EVENT_RUNTIME_STATUS
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)

bool event_slab_available(event_priority_t priority)
{
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);

    if (slab == NULL) {
        return false;
    }

    return k_mem_slab_num_free_get(slab) > 0;
}

uint32_t event_slab_remaining(event_priority_t priority)
{
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);

    if (slab == NULL) {
        return 0;
    }

    return k_mem_slab_num_free_get(slab);
}

void event_get_slab_stats(event_slab_stats_t* stats)
{
    if (stats == NULL) {
        return;
    }

    /* 清零结构体 */
    memset(stats, 0, sizeof(event_slab_stats_t));

#if EVENT_SLAB_CRITICAL_AVAILABLE
    stats->critical_used = k_mem_slab_num_used_get(&event_slab_critical);
    stats->critical_total = CONFIG_EVENT_SLAB_CRITICAL_COUNT;
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
    stats->high_used = k_mem_slab_num_used_get(&event_slab_high);
    stats->high_total = CONFIG_EVENT_SLAB_HIGH_COUNT;
#endif

    stats->normal_used = k_mem_slab_num_used_get(&event_slab_normal);
    stats->normal_total = CONFIG_EVENT_SLAB_NORMAL_COUNT;

#if EVENT_SLAB_256_AVAILABLE
    stats->data_256_used = k_mem_slab_num_used_get(&event_slab_data_256);
    stats->data_256_total = CONFIG_EVENT_SLAB_LARGE_256_COUNT;
#endif

#if EVENT_SLAB_1K_AVAILABLE
    stats->data_1k_used = k_mem_slab_num_used_get(&event_slab_data_1k);
    stats->data_1k_total = CONFIG_EVENT_SLAB_LARGE_1K_COUNT;
#endif

#if EVENT_SLAB_4K_AVAILABLE
    stats->data_4k_used = k_mem_slab_num_used_get(&event_slab_data_4k);
    stats->data_4k_total = CONFIG_EVENT_SLAB_LARGE_4K_COUNT;
#endif

    stats->fallback_count = atomic_get(&g_fallback_count);
}

#endif /* CONFIG_EVENT_RUNTIME_STATUS */

/* =============================================================================
 * 内存调试 API 实现 (Memory Debug API Implementation)
 * 条件编译：CONFIG_EVENT_DEBUG_MEM
 * ============================================================================= */

#if defined(CONFIG_EVENT_DEBUG_MEM) && (CONFIG_EVENT_DEBUG_MEM == 1)

/** 调试跟踪条目 */
typedef struct debug_track_entry {
    void*                  ptr;        /**< 分配的内存指针 */
    size_t                 size;       /**< 分配大小 */
    event_priority_t       priority;   /**< 事件优先级 */
    uint32_t               timestamp;  /**< 分配时间戳 */
    struct debug_track_entry* next;    /**< 链表下一项 */
} debug_track_entry_t;

/** 调试跟踪链表头 */
static debug_track_entry_t* g_debug_track_head = NULL;

/** 调试跟踪链表锁 */
static struct k_mutex g_debug_track_lock;

/** 调试跟踪池大小（可配置） */
#ifndef CONFIG_EVENT_DEBUG_TRACK_COUNT
#define CONFIG_EVENT_DEBUG_TRACK_COUNT 64
#endif

/** 调试跟踪池 */
K_MEM_SLAB_DEFINE(debug_track_slab, sizeof(debug_track_entry_t),
                  CONFIG_EVENT_DEBUG_TRACK_COUNT, sizeof(void*));

/** 调试模块是否已初始化 */
static bool g_debug_initialized = false;

/** 调试模块初始化保护标志 */
static atomic_t g_debug_init_flag = ATOMIC_INIT(0);

/**
 * @brief 初始化调试模块（内部函数）
 *
 * 使用原子标志保护，防止多线程竞争初始化。
 */
static void event_debug_init(void)
{
    if (!g_debug_initialized) {
        while (atomic_test_and_set_bit(&g_debug_init_flag, 0)) {
            k_yield();
        }
        if (!g_debug_initialized) {
            k_mutex_init(&g_debug_track_lock);
            g_debug_initialized = true;
        }
        atomic_clear_bit(&g_debug_init_flag, 0);
    }
}

/**
 * @brief 跟踪内存分配（内部函数）
 *
 * @param ptr 分配的内存指针
 * @param size 分配大小
 * @param priority 事件优先级
 */
void event_debug_track_alloc(void* ptr, size_t size, event_priority_t priority)
{
    if (ptr == NULL) {
        return;
    }

    event_debug_init();

    debug_track_entry_t* entry = NULL;

    if (k_mem_slab_alloc(&debug_track_slab, (void**)&entry, K_NO_WAIT) != 0) {
        LOG_WRN("Debug track slab exhausted");
        return;
    }

    entry->ptr = ptr;
    entry->size = size;
    entry->priority = priority;
    entry->timestamp = k_uptime_get_32();

    k_mutex_lock(&g_debug_track_lock, K_FOREVER);
    entry->next = g_debug_track_head;
    g_debug_track_head = entry;
    k_mutex_unlock(&g_debug_track_lock);
}

/**
 * @brief 取消跟踪内存分配（内部函数）
 *
 * @param ptr 要取消跟踪的内存指针
 */
void event_debug_untrack_alloc(void* ptr)
{
    if (ptr == NULL) {
        return;
    }

    event_debug_init();

    k_mutex_lock(&g_debug_track_lock, K_FOREVER);

    debug_track_entry_t** pp = &g_debug_track_head;

    while (*pp != NULL) {
        if ((*pp)->ptr == ptr) {
            debug_track_entry_t* entry = *pp;
            *pp = entry->next;
            k_mem_slab_free(&debug_track_slab, entry);
            k_mutex_unlock(&g_debug_track_lock);
            return;
        }
        pp = &(*pp)->next;
    }

    k_mutex_unlock(&g_debug_track_lock);
    LOG_WRN("Ptr %p not found in debug track", ptr);
}

uint32_t event_check_leaks(void)
{
    event_debug_init();

    uint32_t count = 0;

    k_mutex_lock(&g_debug_track_lock, K_FOREVER);
    debug_track_entry_t* entry = g_debug_track_head;
    while (entry != NULL) {
        count++;
        entry = entry->next;
    }
    k_mutex_unlock(&g_debug_track_lock);

    return count;
}

void event_dump_leaks(void)
{
    event_debug_init();

    k_mutex_lock(&g_debug_track_lock, K_FOREVER);

    debug_track_entry_t* entry = g_debug_track_head;
    uint32_t count = 0;

    if (entry == NULL) {
        LOG_INF("No memory leaks detected");
        k_mutex_unlock(&g_debug_track_lock);
        return;
    }

    LOG_INF("=== Memory Leak Report ===");

    while (entry != NULL) {
        LOG_INF("Leak #%u: ptr=%p, size=%zu, priority=%d, time=%u",
                count + 1, entry->ptr, entry->size,
                entry->priority, entry->timestamp);
        entry = entry->next;
        count++;
    }

    LOG_INF("Total leaks: %u", count);
    k_mutex_unlock(&g_debug_track_lock);
}

#endif /* CONFIG_EVENT_DEBUG_MEM */
