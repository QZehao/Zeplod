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
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-15       1.0            zeh            初始版本
 *
 */

#ifndef EVENT_MEMORY_H
#define EVENT_MEMORY_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <zeplod/event_system.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Slab 可用性宏 (Slab Availability Macros)
 * 用于条件编译，检查各 Slab 池是否启用
 * ============================================================================= */

#ifdef CONFIG_EVENT_SLAB_ENABLE
#define EVENT_SLAB_ENABLED 1
#else
#define EVENT_SLAB_ENABLED 0
#endif

#if EVENT_SLAB_ENABLED && defined(CONFIG_EVENT_SLAB_CRITICAL_COUNT) && (CONFIG_EVENT_SLAB_CRITICAL_COUNT > 0)
#define EVENT_SLAB_CRITICAL_AVAILABLE 1
#else
#define EVENT_SLAB_CRITICAL_AVAILABLE 0
#endif

#if EVENT_SLAB_ENABLED && defined(CONFIG_EVENT_SLAB_HIGH_COUNT) && (CONFIG_EVENT_SLAB_HIGH_COUNT > 0)
#define EVENT_SLAB_HIGH_AVAILABLE 1
#else
#define EVENT_SLAB_HIGH_AVAILABLE 0
#endif

#ifdef CONFIG_EVENT_SLAB_LARGE_ENABLE
#define EVENT_SLAB_LARGE_AVAILABLE 1
#else
#define EVENT_SLAB_LARGE_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_256_COUNT) && (CONFIG_EVENT_SLAB_LARGE_256_COUNT > 0)
#define EVENT_SLAB_256_AVAILABLE 1
#else
#define EVENT_SLAB_256_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_1K_COUNT) && (CONFIG_EVENT_SLAB_LARGE_1K_COUNT > 0)
#define EVENT_SLAB_1K_AVAILABLE 1
#else
#define EVENT_SLAB_1K_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_4K_COUNT) && (CONFIG_EVENT_SLAB_LARGE_4K_COUNT > 0)
#define EVENT_SLAB_4K_AVAILABLE 1
#else
#define EVENT_SLAB_4K_AVAILABLE 0
#endif

/* =============================================================================
 * 编译时验证 (Compile-time Assertions)
 * ============================================================================= */

/* CRIT-1: NORMAL Slab 计数仅在启用 slab 时强制约束。
 * 禁用 slab 时该 Kconfig 可能未定义或为 0，无条件断言会破坏构建。 */
#if EVENT_SLAB_ENABLED
/** NORMAL Slab 数量至少为 4 */
BUILD_ASSERT(CONFIG_EVENT_SLAB_NORMAL_COUNT >= 4, "CONFIG_EVENT_SLAB_NORMAL_COUNT must be at least 4");
#endif

/** 内联数据大小至少为 4 字节 */
BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE >= 4, "CONFIG_EVENT_INLINE_DATA_SIZE must be at least 4");

/** 内联数据大小不超过 128 字节 */
BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE <= 128, "CONFIG_EVENT_INLINE_DATA_SIZE must not exceed 128");

/* =============================================================================
 * Slab 池声明 (Slab Pool Declarations)
 * 使用 extern 声明外部可访问的 Slab 池
 * ============================================================================= */

#if EVENT_SLAB_ENABLED

#if EVENT_SLAB_CRITICAL_AVAILABLE
/** CRITICAL 优先级事件 Slab 池 */
extern struct k_mem_slab event_slab_critical;
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
/** HIGH 优先级事件 Slab 池 */
extern struct k_mem_slab event_slab_high;
#endif

/** NORMAL/LOW 优先级事件 Slab 池（必须存在） */
extern struct k_mem_slab event_slab_normal;

#if EVENT_SLAB_LARGE_AVAILABLE

#if EVENT_SLAB_256_AVAILABLE
/** 256 字节数据块 Slab 池 */
extern struct k_mem_slab event_slab_data_256;
#endif

#if EVENT_SLAB_1K_AVAILABLE
/** 1KB 数据块 Slab 池 */
extern struct k_mem_slab event_slab_data_1k;
#endif

#if EVENT_SLAB_4K_AVAILABLE
/** 4KB 数据块 Slab 池 */
extern struct k_mem_slab event_slab_data_4k;
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
 * @brief 按指针地址反查事件对象所属的 Slab 池
 *
 * @param ptr 事件对象指针
 * @return 所属 Slab 池指针；不属于任何事件 Slab 池时返回 NULL
 */
struct k_mem_slab* event_memory_resolve_event_slab_for_ptr(void* ptr);

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

/**
 * @brief 根据数据大小选择数据 Slab 池（带级联回退）
 *
 * 与 event_memory_select_data_slab 不同，此函数会检查 Slab 是否有空闲块，
 * 并在首选 Slab 已满时尝试更大的 Slab 池。
 *
 * @param data_len 数据长度（字节）
 * @return 有可用空间的 Slab 池指针，如果所有池都满或不可用返回 NULL
 *
 * @note 级联策略可能导致"内存放大"（如用 4KB 块装 300B 数据）
 * @note 仅用于 event_create_with_data 等非实时路径，RT 路径仍使用原函数
 */
struct k_mem_slab* event_memory_select_data_slab_with_fallback(size_t data_len);

#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE

/**
 * @brief 在 event 上记录数据 slab 来源标记（级联分配后释放须用实际池）
 *
 * @return true 已设置 EVENT_FLAG_SLAB_*，false slab 未知
 */
bool event_memory_data_slab_set_flag(event_t* event, struct k_mem_slab* slab);

/**
 * @brief 由 EVENT_FLAG_SLAB_MASK 查找对应数据 slab 池
 *
 * @param flag flags 中的 EVENT_FLAG_SLAB_* 值（已掩码）
 * @return slab 指针，未知标记时返回 NULL
 */
struct k_mem_slab* event_memory_data_slab_from_flag(uint8_t flag);

/**
 * @brief 按指针地址反查数据 slab 池（标记损坏时的安全释放）
 */
struct k_mem_slab* event_memory_resolve_data_slab_for_ptr(void* ptr);

#endif /* EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE */

/**
 * @brief 记录一次回退到 k_malloc 的计数
 *
 * LOW-NEW-9: 在 event_create_with_data 等函数实际回退到 k_malloc 时调用，
 * 使 fallback_count 统计反映真实的 k_malloc 使用次数。
 */
void event_memory_inc_fallback_count(void);

/**
 * @brief 通知 Slab 池耗尽（内部分配失败时调用）
 *
 * @param priority 相关事件优先级
 * @param slab_name Slab 名称（用于日志与回调）
 */
void event_memory_notify_slab_exhausted(event_priority_t priority, const char* slab_name);

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
    uint32_t critical_used;  /**< CRITICAL 池已用块数 */
    uint32_t critical_total; /**< CRITICAL 池总块数 */
    uint32_t high_used;      /**< HIGH 池已用块数 */
    uint32_t high_total;     /**< HIGH 池总块数 */
    uint32_t normal_used;    /**< NORMAL 池已用块数 */
    uint32_t normal_total;   /**< NORMAL 池总块数 */
    uint32_t data_256_used;  /**< 256B 数据池已用块数 */
    uint32_t data_256_total; /**< 256B 数据池总块数 */
    uint32_t data_1k_used;   /**< 1KB 数据池已用块数 */
    uint32_t data_1k_total;  /**< 1KB 数据池总块数 */
    uint32_t data_4k_used;   /**< 4KB 数据池已用块数 */
    uint32_t data_4k_total;  /**< 4KB 数据池总块数 */
    uint32_t fallback_count; /**< 回退到 k_malloc 的次数 */
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
 * @note 若分配失败发生在 ISR/RT 路径，回调会在相同上下文同步执行，因此回调自身必须 ISR-safe。
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

/**
 * @brief 跟踪内存分配（内部调试接口）
 *
 * @param ptr 分配的内存指针
 * @param size 分配大小
 * @param priority 事件优先级
 */
void event_debug_track_alloc(void* ptr, size_t size, event_priority_t priority);

/**
 * @brief 取消跟踪内存分配（内部调试接口）
 *
 * @param ptr 要取消跟踪的内存指针
 */
void event_debug_untrack_alloc(void* ptr);

#else /* !CONFIG_EVENT_DEBUG_MEM */

static inline void event_debug_track_alloc(void* ptr, size_t size, event_priority_t priority) {
    ARG_UNUSED(ptr);
    ARG_UNUSED(size);
    ARG_UNUSED(priority);
}

static inline void event_debug_untrack_alloc(void* ptr) {
    ARG_UNUSED(ptr);
}

#endif /* CONFIG_EVENT_DEBUG_MEM */

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MEMORY_H */
