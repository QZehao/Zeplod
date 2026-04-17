/**
 * @file event_memory.h
 * @brief 事件系统内存管理模块头文件 (Event Memory Management Header)
 *
 * 提供 Slab 池定义和内存管理函数声明。
 * 支持优先级分层和大数据块分配。
 *
 * 主要特性：
 * - 优先级分层 Slab 池（CRITICAL/HIGH/NORMAL）
 * - 大数据 Slab 池（256B/1KB/4KB）
 * - 运行时状态查询
 * - 内存调试支持（可选）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-15
 *
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-04-15       1.0            zeh            初始版本
 */

#ifndef EVENT_MEMORY_H
#define EVENT_MEMORY_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Slab 可用性宏 (Slab Availability Macros)
 * 用于条件编译，检查各 Slab 池是否启用
 * ============================================================================= */

/** Slab 内存管理是否启用 */
#define EVENT_SLAB_ENABLED            IS_ENABLED(CONFIG_EVENT_SLAB_ENABLE)

/** CRITICAL 优先级 Slab 是否可用 */
#define EVENT_SLAB_CRITICAL_AVAILABLE (EVENT_SLAB_ENABLED && (CONFIG_EVENT_SLAB_CRITICAL_COUNT > 0))

/** HIGH 优先级 Slab 是否可用 */
#define EVENT_SLAB_HIGH_AVAILABLE     (EVENT_SLAB_ENABLED && (CONFIG_EVENT_SLAB_HIGH_COUNT > 0))

/** LARGE 数据 Slab 是否可用 */
#define EVENT_SLAB_LARGE_AVAILABLE    IS_ENABLED(CONFIG_EVENT_SLAB_LARGE_ENABLE)

/** 256 字节数据 Slab 是否可用 */
#define EVENT_SLAB_256_AVAILABLE      (EVENT_SLAB_LARGE_AVAILABLE && (CONFIG_EVENT_SLAB_LARGE_256_COUNT > 0))

/** 1KB 数据 Slab 是否可用 */
#define EVENT_SLAB_1K_AVAILABLE       (EVENT_SLAB_LARGE_AVAILABLE && (CONFIG_EVENT_SLAB_LARGE_1K_COUNT > 0))

/** 4KB 数据 Slab 是否可用 */
#define EVENT_SLAB_4K_AVAILABLE       (EVENT_SLAB_LARGE_AVAILABLE && (CONFIG_EVENT_SLAB_LARGE_4K_COUNT > 0))

/* =============================================================================
 * 编译时验证 (Compile-time Assertions)
 * ============================================================================= */

/** NORMAL Slab 数量至少为 4 */
BUILD_ASSERT(CONFIG_EVENT_SLAB_NORMAL_COUNT >= 4,
             "CONFIG_EVENT_SLAB_NORMAL_COUNT must be at least 4");

/** 内联数据大小至少为 4 字节 */
BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE >= 4,
             "CONFIG_EVENT_INLINE_DATA_SIZE must be at least 4");

/** 内联数据大小不超过 128 字节 */
BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE <= 128,
             "CONFIG_EVENT_INLINE_DATA_SIZE must not exceed 128");

/* =============================================================================
 * Slab 池声明 (Slab Pool Declarations)
 * 使用 K_MEM_SLAB_DECLARE 声明外部可访问的 Slab 池
 * ============================================================================= */

#if EVENT_SLAB_ENABLED

#if EVENT_SLAB_CRITICAL_AVAILABLE
/** CRITICAL 优先级事件 Slab 池 */
K_MEM_SLAB_DECLARE(event_slab_critical);
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
/** HIGH 优先级事件 Slab 池 */
K_MEM_SLAB_DECLARE(event_slab_high);
#endif

/** NORMAL/LOW 优先级事件 Slab 池（必须存在） */
K_MEM_SLAB_DECLARE(event_slab_normal);

#if EVENT_SLAB_LARGE_AVAILABLE

#if EVENT_SLAB_256_AVAILABLE
/** 256 字节数据块 Slab 池 */
K_MEM_SLAB_DECLARE(event_slab_data_256);
#endif

#if EVENT_SLAB_1K_AVAILABLE
/** 1KB 数据块 Slab 池 */
K_MEM_SLAB_DECLARE(event_slab_data_1k);
#endif

#if EVENT_SLAB_4K_AVAILABLE
/** 4KB 数据块 Slab 池 */
K_MEM_SLAB_DECLARE(event_slab_data_4k);
#endif

#endif /* EVENT_SLAB_LARGE_AVAILABLE */

#endif /* EVENT_SLAB_ENABLED */

/* =============================================================================
 * 内部分配函数 (Internal Allocation Functions)
 * ============================================================================= */

/**
 * @brief 根据优先级选择事件 Slab 池
 *
 * @param priority 事件优先级
 * @return 对应的 Slab 池指针，如果无专用池则返回 NORMAL 池
 *
 * @note 此函数为内部使用，供 event_create 等函数调用
 */
struct k_mem_slab* event_memory_select_event_slab(event_priority_t priority);

/**
 * @brief 根据数据大小选择数据 Slab 池
 *
 * @param data_len 数据长度（字节）
 * @return 对应的 Slab 池指针，如果超出 Slab 范围返回 NULL
 *
 * @note 选择规则：
 *       - data_len <= 256: 256B 池
 *       - data_len <= 1024: 1KB 池
 *       - data_len <= 4096: 4KB 池
 *       - data_len > 4096: NULL（使用 k_malloc）
 */
struct k_mem_slab* event_memory_select_data_slab(size_t data_len);

/* =============================================================================
 * 运行时状态 API (Runtime Status API)
 * 条件编译：CONFIG_EVENT_RUNTIME_STATUS
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)

/**
 * @brief 检查指定优先级的 Slab 是否可用
 *
 * @param priority 事件优先级
 * @return true 有可用块，false Slab 已耗尽或不可用
 */
bool event_slab_available(event_priority_t priority);

/**
 * @brief 获取指定优先级 Slab 的剩余块数
 *
 * @param priority 事件优先级
 * @return 剩余可用块数，如果 Slab 不可用返回 0
 */
uint32_t event_slab_remaining(event_priority_t priority);

/**
 * @brief Slab 统计信息结构体
 */
typedef struct {
    uint32_t critical_used;     /**< CRITICAL 池已用块数 */
    uint32_t critical_total;    /**< CRITICAL 池总块数 */
    uint32_t high_used;         /**< HIGH 池已用块数 */
    uint32_t high_total;        /**< HIGH 池总块数 */
    uint32_t normal_used;       /**< NORMAL 池已用块数 */
    uint32_t normal_total;      /**< NORMAL 池总块数 */
    uint32_t data_256_used;     /**< 256B 数据池已用块数 */
    uint32_t data_256_total;    /**< 256B 数据池总块数 */
    uint32_t data_1k_used;      /**< 1KB 数据池已用块数 */
    uint32_t data_1k_total;     /**< 1KB 数据池总块数 */
    uint32_t data_4k_used;      /**< 4KB 数据池已用块数 */
    uint32_t data_4k_total;     /**< 4KB 数据池总块数 */
    uint32_t fallback_count;    /**< 回退到 k_malloc 的次数 */
} event_slab_stats_t;

/**
 * @brief 获取所有 Slab 池的统计信息
 *
 * @param stats 输出参数，接收统计信息
 *
 * @note 此函数会遍历所有 Slab 池，可能耗时较长
 */
void event_get_slab_stats(event_slab_stats_t* stats);

#endif /* CONFIG_EVENT_RUNTIME_STATUS */

/* =============================================================================
 * Slab 耗尽回调 (Slab Exhausted Callback)
 * 条件编译：CONFIG_EVENT_SLAB_EXHAUSTED_CB
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_EXHAUSTED_CB) && (CONFIG_EVENT_SLAB_EXHAUSTED_CB == 1)

/**
 * @brief Slab 耗尽回调函数类型
 *
 * @param priority 触发耗尽的优先级
 * @param slab_name Slab 名称（用于日志）
 */
typedef void (*event_slab_exhausted_cb_t)(event_priority_t priority, const char* slab_name);

/**
 * @brief 注册 Slab 耗尽回调函数
 *
 * @param cb 回调函数指针，传入 NULL 清除回调
 *
 * @note 同一时间只能有一个回调生效
 * @note 回调在分配失败时同步调用，应避免阻塞操作
 */
void event_register_slab_exhausted_cb(event_slab_exhausted_cb_t cb);

#endif /* CONFIG_EVENT_SLAB_EXHAUSTED_CB */

/* =============================================================================
 * 内存调试 API (Memory Debug API)
 * 条件编译：CONFIG_EVENT_DEBUG_MEM
 * ============================================================================= */

#if defined(CONFIG_EVENT_DEBUG_MEM) && (CONFIG_EVENT_DEBUG_MEM == 1)

/**
 * @brief 检查内存泄漏
 *
 * @return 当前未释放的分配数量
 *
 * @note 仅当 CONFIG_EVENT_DEBUG_MEM 启用时可用
 */
uint32_t event_check_leaks(void);

/**
 * @brief 打印内存泄漏详情
 *
 * 打印所有未释放的分配信息到日志。
 *
 * @note 仅当 CONFIG_EVENT_DEBUG_MEM 启用时可用
 */
void event_dump_leaks(void);

#endif /* CONFIG_EVENT_DEBUG_MEM */

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MEMORY_H */
