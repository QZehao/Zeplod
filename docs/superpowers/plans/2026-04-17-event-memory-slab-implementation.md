# 事件系统 Slab 内存管理实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为事件系统实现基于 Slab 的内存管理，满足硬实时 O(1) 分配要求。

**Architecture:** 重构 event_t 结构体支持内联数据；添加按优先级隔离的 Slab 池；分离实时安全 API 和灵活模式 API；可裁剪扩展功能。

**Tech Stack:** Zephyr RTOS, k_mem_slab, Kconfig

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `Kconfig` | 修改 | 添加 Slab 配置项 |
| `src/core/event_system.h` | 修改 | 重构 event_t 结构体，声明新 API |
| `src/core/event_system.c` | 修改 | 实现 Slab 池和分配/释放逻辑 |
| `src/core/event_memory.h` | 创建 | Slab 池定义和内存管理内部接口 |
| `src/core/event_memory.c` | 创建 | Slab 池实现和扩展功能 |
| `tests/unit/test_event_memory.c` | 创建 | 单元测试 |

---

## Task 1: Kconfig 配置项

**Files:**
- Modify: `Kconfig`

- [ ] **Step 1: 在 Kconfig 的 Event System Configuration 菜单末尾添加 Slab 配置**

在 `endmenu` 之前添加（约第53行之前）：

```kconfig
	# =============================================================================
	# Event Structure Size Configuration
	# =============================================================================

	choice
		prompt "Event structure size"
		default EVENT_STRUCT_SIZE_64
		help
		  Select event_t structure size. Larger size provides more inline
		  data capacity but consumes more memory per event.

	config EVENT_STRUCT_SIZE_32
		bool "32 bytes (compact)"
		help
		  Minimal footprint. Inline data: 16 bytes.
		  Recommended for < 16KB RAM devices.

	config EVENT_STRUCT_SIZE_64
		bool "64 bytes (balanced)"
		help
		  Good balance. Inline data: 48 bytes.
		  Recommended for 16KB-256KB RAM devices.

	config EVENT_STRUCT_SIZE_128
		bool "128 bytes (high capacity)"
		help
		  Maximum inline capacity. Inline data: 112 bytes.
		  Recommended for > 256KB RAM devices.

	endchoice

	config EVENT_STRUCT_SIZE
		int
		default 32 if EVENT_STRUCT_SIZE_32
		default 64 if EVENT_STRUCT_SIZE_64
		default 128 if EVENT_STRUCT_SIZE_128

	config EVENT_INLINE_DATA_SIZE
		int
		default 16 if EVENT_STRUCT_SIZE_32
		default 48 if EVENT_STRUCT_SIZE_64
		default 112 if EVENT_STRUCT_SIZE_128

	# =============================================================================
	# Event Slab Memory Configuration
	# =============================================================================

	config EVENT_SLAB_ENABLE
		bool "Enable slab-based memory management"
		default y
		help
		  Use k_mem_slab for deterministic O(1) memory allocation.
		  Disable to fall back to k_malloc (not real-time safe).

	if EVENT_SLAB_ENABLE

	config EVENT_SLAB_CRITICAL_COUNT
		int "CRITICAL priority event pool size"
		default 8
		range 0 64
		help
		  Number of event_t blocks reserved for CRITICAL priority.
		  Set to 0 to share with NORMAL pool.

	config EVENT_SLAB_HIGH_COUNT
		int "HIGH priority event pool size"
		default 16
		range 0 64
		help
		  Number of event_t blocks reserved for HIGH priority.
		  Set to 0 to share with NORMAL pool.

	config EVENT_SLAB_NORMAL_COUNT
		int "NORMAL/LOW priority event pool size"
		default 32
		range 4 128
		help
		  Number of event_t blocks for NORMAL and LOW priority events.

	config EVENT_SLAB_LARGE_ENABLE
		bool "Enable large data slab pools"
		default y
		help
		  Enable slab pools for event data larger than inline threshold.
		  Disable to use k_malloc for large data (not real-time safe).

	if EVENT_SLAB_LARGE_ENABLE

	config EVENT_SLAB_LARGE_256_COUNT
		int "256-byte data block count"
		default 8
		range 0 32

	config EVENT_SLAB_LARGE_1K_COUNT
		int "1KB data block count"
		default 4
		range 0 16

	config EVENT_SLAB_LARGE_4K_COUNT
		int "4KB data block count"
		default 2
		range 0 8

	endif # EVENT_SLAB_LARGE_ENABLE

	endif # EVENT_SLAB_ENABLE

	# =============================================================================
	# Event Extended Features
	# =============================================================================

	config EVENT_SLAB_EXHAUSTED_CB
		bool "Enable slab exhausted callback"
		default n
		help
		  Invoke callback when a slab pool is exhausted.

	config EVENT_DEBUG_MEM
		bool "Enable memory debugging support"
		default n
		help
		  Enable leak detection and memory diagnostics.
		  Increases memory usage, for development only.

	config EVENT_RUNTIME_STATUS
		bool "Enable runtime pool status query"
		default y
		help
		  Provide APIs to query slab availability at runtime.

	config EVENT_SLAB_STATS_DETAILED
		bool "Enable detailed slab statistics"
		default n
		depends on EVENT_RUNTIME_STATUS
		help
		  Track detailed statistics including allocation counts
		  and high watermarks.
```

- [ ] **Step 2: 验证 Kconfig 语法**

```bash
# 检查 Kconfig 语法（Zephyr 提供 kconfiglib）
python -c "from kconfiglib import Kconfig; Kconfig('Kconfig')"
```

Expected: 无语法错误输出

- [ ] **Step 3: Commit**

```bash
git add Kconfig
git commit -m "$(cat <<'EOF'
feat(event): add Kconfig options for slab memory management

Add configurable options for:
- Event structure size (32B/64B/128B)
- Priority-based slab pool sizes
- Large data slab pools
- Extended features (exhausted callback, debug, runtime status)

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: event_t 结构体重构

**Files:**
- Modify: `src/core/event_system.h`

- [ ] **Step 1: 备份原有 event_t 定义位置**

记录原 event_t 结构体的行号范围（约142-150行），后续需要删除并替换。

- [ ] **Step 2: 添加编译时验证和新的 event_t 定义**

在 `event_t` 原定义之前添加配置宏和标志位定义，然后替换整个 event_t 结构体：

```c
/* =============================================================================
 * 内存配置宏 (Memory Configuration Macros)
 * ============================================================================= */

/** 内联数据大小（从 Kconfig 获取，默认 48 字节） */
#ifndef CONFIG_EVENT_INLINE_DATA_SIZE
#define CONFIG_EVENT_INLINE_DATA_SIZE   48
#endif

/** 事件结构体大小（从 Kconfig 获取，默认 64 字节） */
#ifndef CONFIG_EVENT_STRUCT_SIZE
#define CONFIG_EVENT_STRUCT_SIZE        64
#endif

/* =============================================================================
 * 事件标志位定义 (Event Flags)
 * ============================================================================= */

/** 数据内联存储 */
#define EVENT_FLAG_DATA_INLINE   0x01U

/** 数据动态分配 */
#define EVENT_FLAG_DATA_DYNAMIC  0x02U

/** event_t 来自 slab 池 */
#define EVENT_FLAG_FROM_SLAB     0x04U

/* =============================================================================
 * 类型定义 (Type Definitions)
 * ============================================================================= */

/**
 * @brief 事件数据结构
 *
 * 内存布局（以 64B 为例）：
 * ┌────────────────────────────────┐
 * │ type(1) priority(1) flags(1) ? │  4B
 * │ timestamp                      │  4B
 * │ source_id                      │  4B
 * │ data_len                       │  4B
 * ├────────────────────────────────┤  16B 头部
 * │ inline_data[48] 或 ptr(8)      │ 48B
 * └────────────────────────────────┘  64B 总计
 *
 * 数据存储策略：
 * - data_len ≤ INLINE_DATA_SIZE: 内联存储，无额外分配
 * - data_len > INLINE_DATA_SIZE: 从 slab/k_malloc 分配
 *
 * @note 结构体大小由 CONFIG_EVENT_STRUCT_SIZE 控制
 */
typedef struct {
    uint8_t          type;           /**< 事件类型标识符 */
    uint8_t          priority;       /**< 事件优先级 */
    uint8_t          flags;          /**< 标志位 (EVENT_FLAG_*) */
    uint8_t          reserved;       /**< 预留扩展 */
    uint32_t         timestamp;      /**< 事件创建时间戳（毫秒 uptime） */
    uint32_t         source_id;      /**< 源模块/组件 ID */
    uint32_t         data_len;       /**< 事件数据长度（字节） */
    union {
        uint8_t  inline_data[CONFIG_EVENT_INLINE_DATA_SIZE]; /**< 内联数据 */
        void*    ptr;                                             /**< 外部数据指针 */
    } data;
} event_t;

/* 编译时验证结构体大小 */
BUILD_ASSERT(sizeof(event_t) == CONFIG_EVENT_STRUCT_SIZE,
             "event_t size mismatch with CONFIG_EVENT_STRUCT_SIZE");
```

注意：需要添加 `#include <zephyr/sys/util.h>` 或确保 BUILD_ASSERT 宏可用（通常通过 `#include <zephyr/kernel.h>` 已经包含）。

- [ ] **Step 3: 检查现有代码中对 event_t 字段的引用**

确认 `event->is_dynamic` 需要改为 `event->flags & EVENT_FLAG_DATA_DYNAMIC`。

- [ ] **Step 4: Commit**

```bash
git add src/core/event_system.h
git commit -m "$(cat <<'EOF'
refactor(event): restructure event_t for inline data support

- Replace is_dynamic bool with flags field
- Add union for inline_data and ptr
- Add BUILD_ASSERT for size validation
- Maintain backward compatibility via flags

Breaking: Direct access to is_dynamic field must use flags

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: 创建 event_memory 模块

**Files:**
- Create: `src/core/event_memory.h`
- Create: `src/core/event_memory.c`

- [ ] **Step 1: 创建 event_memory.h 头文件**

```c
/**
 * @file event_memory.h
 * @brief 事件系统内存管理模块
 *
 * 基于 k_mem_slab 的确定性内存分配，支持：
 * - 按优先级隔离的内存池
 * - 内联数据优化
 * - 实时安全 API
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-17
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
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_ENABLE) && CONFIG_EVENT_SLAB_ENABLE
#define EVENT_SLAB_ENABLED 1
#else
#define EVENT_SLAB_ENABLED 0
#endif

#if EVENT_SLAB_ENABLED && defined(CONFIG_EVENT_SLAB_CRITICAL_COUNT) && \
    CONFIG_EVENT_SLAB_CRITICAL_COUNT > 0
#define EVENT_SLAB_CRITICAL_AVAILABLE 1
#else
#define EVENT_SLAB_CRITICAL_AVAILABLE 0
#endif

#if EVENT_SLAB_ENABLED && defined(CONFIG_EVENT_SLAB_HIGH_COUNT) && \
    CONFIG_EVENT_SLAB_HIGH_COUNT > 0
#define EVENT_SLAB_HIGH_AVAILABLE 1
#else
#define EVENT_SLAB_HIGH_AVAILABLE 0
#endif

#if EVENT_SLAB_ENABLED && defined(CONFIG_EVENT_SLAB_LARGE_ENABLE) && \
    CONFIG_EVENT_SLAB_LARGE_ENABLE
#define EVENT_SLAB_LARGE_AVAILABLE 1
#else
#define EVENT_SLAB_LARGE_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_256_COUNT) && \
    CONFIG_EVENT_SLAB_LARGE_256_COUNT > 0
#define EVENT_SLAB_256_AVAILABLE 1
#else
#define EVENT_SLAB_256_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_1K_COUNT) && \
    CONFIG_EVENT_SLAB_LARGE_1K_COUNT > 0
#define EVENT_SLAB_1K_AVAILABLE 1
#else
#define EVENT_SLAB_1K_AVAILABLE 0
#endif

#if EVENT_SLAB_LARGE_AVAILABLE && defined(CONFIG_EVENT_SLAB_LARGE_4K_COUNT) && \
    CONFIG_EVENT_SLAB_LARGE_4K_COUNT > 0
#define EVENT_SLAB_4K_AVAILABLE 1
#else
#define EVENT_SLAB_4K_AVAILABLE 0
#endif

/* =============================================================================
 * 编译时验证 (Compile-time Validation)
 * ============================================================================= */

BUILD_ASSERT(CONFIG_EVENT_SLAB_NORMAL_COUNT >= 4,
             "NORMAL slab count must be at least 4");

BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE >= 4,
             "INLINE_DATA_SIZE must be at least 4 bytes");

BUILD_ASSERT(CONFIG_EVENT_INLINE_DATA_SIZE <= 128,
             "INLINE_DATA_SIZE must not exceed 128 bytes");

/* =============================================================================
 * Slab 池定义 (Slab Pool Definitions)
 * ============================================================================= */

#if EVENT_SLAB_ENABLED

/** CRITICAL 优先级事件池 */
#if EVENT_SLAB_CRITICAL_AVAILABLE
K_MEM_SLAB_DECLARE(event_slab_critical);
#endif

/** HIGH 优先级事件池 */
#if EVENT_SLAB_HIGH_AVAILABLE
K_MEM_SLAB_DECLARE(event_slab_high);
#endif

/** NORMAL/LOW 优先级事件池 */
K_MEM_SLAB_DECLARE(event_slab_normal);

/** 大数据池 - 256B */
#if EVENT_SLAB_256_AVAILABLE
K_MEM_SLAB_DECLARE(event_slab_data_256);
#endif

/** 大数据池 - 1KB */
#if EVENT_SLAB_1K_AVAILABLE
K_MEM_SLAB_DECLARE(event_slab_data_1k);
#endif

/** 大数据池 - 4KB */
#if EVENT_SLAB_4K_AVAILABLE
K_MEM_SLAB_DECLARE(event_slab_data_4k);
#endif

#endif /* EVENT_SLAB_ENABLED */

/* =============================================================================
 * 内部分配函数 (Internal Allocation Functions)
 * ============================================================================= */

/**
 * @brief 根据优先级选择 event_t slab 池
 *
 * @param priority 事件优先级
 * @return slab 指针，无可用池返回 NULL
 */
struct k_mem_slab* event_memory_select_event_slab(event_priority_t priority);

/**
 * @brief 根据数据大小选择数据 slab 池
 *
 * @param data_len 数据长度
 * @return slab 指针，无合适池返回 NULL
 */
struct k_mem_slab* event_memory_select_data_slab(size_t data_len);

/* =============================================================================
 * 运行时状态 API (Runtime Status API)
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && CONFIG_EVENT_RUNTIME_STATUS

/**
 * @brief 检查指定优先级的 slab 是否有可用块
 */
bool event_slab_available(event_priority_t priority);

/**
 * @brief 获取指定优先级 slab 的剩余块数
 */
uint32_t event_slab_remaining(event_priority_t priority);

/**
 * @brief Slab 统计信息结构
 */
typedef struct {
    uint32_t critical_used;    /**< CRITICAL 池已用块数 */
    uint32_t critical_total;   /**< CRITICAL 池总块数 */
    uint32_t high_used;        /**< HIGH 池已用块数 */
    uint32_t high_total;       /**< HIGH 池总块数 */
    uint32_t normal_used;      /**< NORMAL 池已用块数 */
    uint32_t normal_total;     /**< NORMAL 池总块数 */
    uint32_t data_256_used;    /**< 256B 数据池已用 */
    uint32_t data_1k_used;     /**< 1KB 数据池已用 */
    uint32_t data_4k_used;     /**< 4KB 数据池已用 */
    uint32_t fallback_count;   /**< 回退 k_malloc 次数 */
} event_slab_stats_t;

/**
 * @brief 获取 Slab 统计信息
 */
void event_get_slab_stats(event_slab_stats_t* stats);

#endif /* CONFIG_EVENT_RUNTIME_STATUS */

/* =============================================================================
 * Slab 耗尽回调 (Slab Exhausted Callback)
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_EXHAUSTED_CB) && CONFIG_EVENT_SLAB_EXHAUSTED_CB

/**
 * @brief Slab 耗尽回调类型
 */
typedef void (*event_slab_exhausted_cb_t)(event_priority_t priority, size_t data_size);

/**
 * @brief 注册 Slab 耗尽回调
 */
void event_register_slab_exhausted_cb(event_slab_exhausted_cb_t cb);

#endif /* CONFIG_EVENT_SLAB_EXHAUSTED_CB */

/* =============================================================================
 * 内存调试 API (Memory Debug API)
 * ============================================================================= */

#if defined(CONFIG_EVENT_DEBUG_MEM) && CONFIG_EVENT_DEBUG_MEM

/**
 * @brief 检查是否有内存泄漏
 * @return 未释放的事件数量
 */
uint32_t event_check_leaks(void);

/**
 * @brief 打印所有未释放事件的详细信息
 */
void event_dump_leaks(void);

#endif /* CONFIG_EVENT_DEBUG_MEM */

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MEMORY_H */
```

- [ ] **Step 2: 创建 event_memory.c 实现文件**

```c
/**
 * @file event_memory.c
 * @brief 事件系统内存管理实现
 */

#include "event_memory.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(event_memory, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Slab 池定义 (Slab Pool Definitions)
 * ============================================================================= */

#if EVENT_SLAB_ENABLED

#if EVENT_SLAB_CRITICAL_AVAILABLE
K_MEM_SLAB_DEFINE(event_slab_critical, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_CRITICAL_COUNT, 4);
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
K_MEM_SLAB_DEFINE(event_slab_high, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_HIGH_COUNT, 4);
#endif

K_MEM_SLAB_DEFINE(event_slab_normal, CONFIG_EVENT_STRUCT_SIZE,
                  CONFIG_EVENT_SLAB_NORMAL_COUNT, 4);

#if EVENT_SLAB_LARGE_AVAILABLE

#if EVENT_SLAB_256_AVAILABLE
K_MEM_SLAB_DEFINE(event_slab_data_256, 256,
                  CONFIG_EVENT_SLAB_LARGE_256_COUNT, 4);
#endif

#if EVENT_SLAB_1K_AVAILABLE
K_MEM_SLAB_DEFINE(event_slab_data_1k, 1024,
                  CONFIG_EVENT_SLAB_LARGE_1K_COUNT, 4);
#endif

#if EVENT_SLAB_4K_AVAILABLE
K_MEM_SLAB_DEFINE(event_slab_data_4k, 4096,
                  CONFIG_EVENT_SLAB_LARGE_4K_COUNT, 4);
#endif

#endif /* EVENT_SLAB_LARGE_AVAILABLE */

#endif /* EVENT_SLAB_ENABLED */

/* =============================================================================
 * 统计计数器 (Statistics Counters)
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && CONFIG_EVENT_RUNTIME_STATUS
static atomic_t g_fallback_count;
#endif

/* =============================================================================
 * Slab 耗尽回调 (Slab Exhausted Callback)
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_EXHAUSTED_CB) && CONFIG_EVENT_SLAB_EXHAUSTED_CB
static event_slab_exhausted_cb_t g_slab_exhausted_cb;

void event_register_slab_exhausted_cb(event_slab_exhausted_cb_t cb) {
    g_slab_exhausted_cb = cb;
}

static void notify_slab_exhausted(event_priority_t priority, size_t data_size) {
    if (g_slab_exhausted_cb != NULL) {
        g_slab_exhausted_cb(priority, data_size);
    }
}
#else
static inline void notify_slab_exhausted(event_priority_t priority, size_t data_size) {
    (void)priority;
    (void)data_size;
}
#endif

/* =============================================================================
 * 内部分配函数实现 (Internal Functions Implementation)
 * ============================================================================= */

struct k_mem_slab* event_memory_select_event_slab(event_priority_t priority) {
#if EVENT_SLAB_ENABLED
    switch (priority) {
#if EVENT_SLAB_CRITICAL_AVAILABLE
    case EVENT_PRIORITY_CRITICAL:
        return &event_slab_critical;
#endif
#if EVENT_SLAB_HIGH_AVAILABLE
    case EVENT_PRIORITY_HIGH:
        return &event_slab_high;
#endif
    default:
        return &event_slab_normal;
    }
#else
    (void)priority;
    return NULL;
#endif
}

struct k_mem_slab* event_memory_select_data_slab(size_t data_len) {
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
    if (data_len <= 256) {
#if EVENT_SLAB_256_AVAILABLE
        return &event_slab_data_256;
#endif
    }
    if (data_len <= 1024) {
#if EVENT_SLAB_1K_AVAILABLE
        return &event_slab_data_1k;
#endif
    }
    if (data_len <= 4096) {
#if EVENT_SLAB_4K_AVAILABLE
        return &event_slab_data_4k;
#endif
    }
#else
    (void)data_len;
#endif
    return NULL;
}

/* =============================================================================
 * 运行时状态 API 实现 (Runtime Status API Implementation)
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && CONFIG_EVENT_RUNTIME_STATUS

bool event_slab_available(event_priority_t priority) {
#if EVENT_SLAB_ENABLED
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);
    if (slab == NULL) {
        return false;
    }
    return k_mem_slab_num_free_get(slab) > 0;
#else
    (void)priority;
    return false;
#endif
}

uint32_t event_slab_remaining(event_priority_t priority) {
#if EVENT_SLAB_ENABLED
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);
    if (slab == NULL) {
        return 0;
    }
    return k_mem_slab_num_free_get(slab);
#else
    (void)priority;
    return 0;
#endif
}

void event_get_slab_stats(event_slab_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));

#if EVENT_SLAB_ENABLED
#if EVENT_SLAB_CRITICAL_AVAILABLE
    stats->critical_total = CONFIG_EVENT_SLAB_CRITICAL_COUNT;
    stats->critical_used = CONFIG_EVENT_SLAB_CRITICAL_COUNT -
                           k_mem_slab_num_free_get(&event_slab_critical);
#endif
#if EVENT_SLAB_HIGH_AVAILABLE
    stats->high_total = CONFIG_EVENT_SLAB_HIGH_COUNT;
    stats->high_used = CONFIG_EVENT_SLAB_HIGH_COUNT -
                       k_mem_slab_num_free_get(&event_slab_high);
#endif
    stats->normal_total = CONFIG_EVENT_SLAB_NORMAL_COUNT;
    stats->normal_used = CONFIG_EVENT_SLAB_NORMAL_COUNT -
                         k_mem_slab_num_free_get(&event_slab_normal);

#if EVENT_SLAB_LARGE_AVAILABLE
#if EVENT_SLAB_256_AVAILABLE
    stats->data_256_used = CONFIG_EVENT_SLAB_LARGE_256_COUNT -
                           k_mem_slab_num_free_get(&event_slab_data_256);
#endif
#if EVENT_SLAB_1K_AVAILABLE
    stats->data_1k_used = CONFIG_EVENT_SLAB_LARGE_1K_COUNT -
                          k_mem_slab_num_free_get(&event_slab_data_1k);
#endif
#if EVENT_SLAB_4K_AVAILABLE
    stats->data_4k_used = CONFIG_EVENT_SLAB_LARGE_4K_COUNT -
                          k_mem_slab_num_free_get(&event_slab_data_4k);
#endif
#endif

    stats->fallback_count = atomic_get(&g_fallback_count);
#endif
}

#endif /* CONFIG_EVENT_RUNTIME_STATUS */

/* =============================================================================
 * 内存调试 API 实现 (Memory Debug API Implementation)
 * ============================================================================= */

#if defined(CONFIG_EVENT_DEBUG_MEM) && CONFIG_EVENT_DEBUG_MEM

/* 分配跟踪链表 */
typedef struct alloc_node {
    event_t* event;
    uint32_t alloc_time;
    const char* func;
    int line;
    struct alloc_node* next;
} alloc_node_t;

static alloc_node_t* g_alloc_list;
static struct k_mutex g_debug_lock;

void event_debug_init(void) {
    k_mutex_init(&g_debug_lock);
    g_alloc_list = NULL;
}

void event_debug_track_alloc(event_t* event, const char* func, int line) {
    alloc_node_t* node = k_malloc(sizeof(alloc_node_t));
    if (node == NULL) {
        LOG_ERR("Failed to allocate debug tracking node");
        return;
    }

    k_mutex_lock(&g_debug_lock, K_FOREVER);
    node->event = event;
    node->alloc_time = k_uptime_get_32();
    node->func = func;
    node->line = line;
    node->next = g_alloc_list;
    g_alloc_list = node;
    k_mutex_unlock(&g_debug_lock);
}

void event_debug_untrack_alloc(event_t* event) {
    k_mutex_lock(&g_debug_lock, K_FOREVER);
    alloc_node_t** pp = &g_alloc_list;
    while (*pp != NULL) {
        if ((*pp)->event == event) {
            alloc_node_t* tmp = *pp;
            *pp = (*pp)->next;
            k_free(tmp);
            k_mutex_unlock(&g_debug_lock);
            return;
        }
        pp = &(*pp)->next;
    }
    k_mutex_unlock(&g_debug_lock);
}

uint32_t event_check_leaks(void) {
    k_mutex_lock(&g_debug_lock, K_FOREVER);
    uint32_t count = 0;
    alloc_node_t* node = g_alloc_list;
    while (node != NULL) {
        count++;
        node = node->next;
    }
    k_mutex_unlock(&g_debug_lock);
    return count;
}

void event_dump_leaks(void) {
    k_mutex_lock(&g_debug_lock, K_FOREVER);
    alloc_node_t* node = g_alloc_list;
    while (node != NULL) {
        LOG_ERR("Leak: event=%p, type=%d, prio=%d, func=%s, line=%d, age=%ums",
                (void*)node->event, node->event->type, node->event->priority,
                node->func ? node->func : "unknown", node->line,
                k_uptime_get_32() - node->alloc_time);
        node = node->next;
    }
    k_mutex_unlock(&g_debug_lock);
}

#endif /* CONFIG_EVENT_DEBUG_MEM */
```

- [ ] **Step 3: Commit**

```bash
git add src/core/event_memory.h src/core/event_memory.c
git commit -m "$(cat <<'EOF'
feat(event): add event_memory module for slab management

- Define priority-based slab pools (CRITICAL/HIGH/NORMAL)
- Define large data slab pools (256B/1KB/4KB)
- Implement slab selection functions
- Add runtime status query APIs
- Add slab exhausted callback support
- Add memory debugging support (optional)

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: 实现实时安全 API

**Files:**
- Modify: `src/core/event_system.h`
- Modify: `src/core/event_system.c`

- [ ] **Step 1: 在 event_system.h 添加实时安全 API 声明**

在现有 `event_create` 声明之前添加：

```c
/* =============================================================================
 * 实时安全 API (Real-time Safe API)
 * ============================================================================= */

/**
 * @brief 创建事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @return 指向新事件的指针，失败返回 NULL
 *
 * @note 完全从 slab 池分配，分配时间 O(1) 确定
 * @note Slab 耗尽时返回 NULL，不回退 k_malloc
 * @note 无 Slab 配置时返回 NULL
 */
event_t* event_create_rt(event_type_t type, event_priority_t priority);

/**
 * @brief 创建带数据的事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要附加的数据指针
 * @param data_len 数据长度（字节）
 * @return 指向新事件的指针，失败返回 NULL
 *
 * @note 数据存储策略：
 *   - data_len ≤ INLINE_DATA_SIZE: 内联存储，无额外分配
 *   - data_len > INLINE_DATA_SIZE: 从 slab 池分配
 *   - 无可用 slab 或 slab 满: 返回 NULL
 * @note 完全实时安全，永不回退 k_malloc
 */
event_t* event_create_with_data_rt(event_type_t type, event_priority_t priority,
                                    const void* data, size_t data_len);

/**
 * @brief 发布事件并复制数据（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 要复制的数据指针
 * @param data_len 数据长度（字节）
 * @return EVENT_OK 成功，其他错误码见 event_status_t
 *
 * @note 完全实时安全，内存不足时返回错误
 */
event_status_t event_publish_copy_rt(event_type_t type, event_priority_t priority,
                                      const void* data, size_t data_len);

/**
 * @brief 从 ISR 创建事件（实时安全）
 *
 * @param type 事件类型 ID
 * @param priority 事件优先级
 * @param data 数据指针
 * @param data_len 数据长度
 * @return 事件指针，失败返回 NULL
 *
 * @note 等同于 event_create_with_data_rt，明确 ISR 上下文使用
 */
static inline event_t* event_create_from_isr(event_type_t type,
                                              event_priority_t priority,
                                              const void* data, size_t data_len) {
    return event_create_with_data_rt(type, priority, data, data_len);
}
```

- [ ] **Step 2: 在 event_system.c 实现 event_create_rt**

在现有 `event_create` 函数之前添加实现：

```c
event_t* event_create_rt(event_type_t type, event_priority_t priority) {
    event_t* event = NULL;

#if EVENT_SLAB_ENABLED
    struct k_mem_slab* slab = event_memory_select_event_slab(priority);
    int ret = k_mem_slab_alloc(slab, (void**)&event, K_NO_WAIT);

    /* 尝试降级借用低优先级池 */
    if (ret != 0 && priority == EVENT_PRIORITY_CRITICAL && EVENT_SLAB_HIGH_AVAILABLE) {
        ret = k_mem_slab_alloc(&event_slab_high, (void**)&event, K_NO_WAIT);
    }
    if (ret != 0 && priority <= EVENT_PRIORITY_HIGH) {
        ret = k_mem_slab_alloc(&event_slab_normal, (void**)&event, K_NO_WAIT);
    }

    if (ret != 0) {
        notify_slab_exhausted(priority, 0);
        LOG_WRN("Event slab exhausted for priority %d", priority);
        return NULL;
    }

    event->flags = EVENT_FLAG_FROM_SLAB;
#else
    /* 无 Slab 配置，rt 版本不可用 */
    LOG_ERR("event_create_rt called but slab not enabled");
    return NULL;
#endif

    /* 初始化字段 */
    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = 0;
    event->reserved = 0;
    memset(event->data.inline_data, 0, CONFIG_EVENT_INLINE_DATA_SIZE);

    return event;
}
```

- [ ] **Step 3: 实现 event_create_with_data_rt**

```c
event_t* event_create_with_data_rt(event_type_t type, event_priority_t priority,
                                    const void* data, size_t data_len) {
    if (data == NULL || data_len == 0) {
        return event_create_rt(type, priority);
    }

    /* 大数据检查 slab 可用性 */
    if (data_len > CONFIG_EVENT_INLINE_DATA_SIZE) {
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
        struct k_mem_slab* data_slab = event_memory_select_data_slab(data_len);
        if (data_slab == NULL) {
            LOG_WRN("Data size %zu exceeds largest slab", data_len);
            return NULL;
        }
#else
        LOG_WRN("Large data requested but no slab configured");
        return NULL;
#endif
    }

    event_t* event = event_create_rt(type, priority);
    if (event == NULL) {
        return NULL;
    }

    event->data_len = (uint32_t)data_len;

    /* 小数据：内联存储 */
    if (data_len <= CONFIG_EVENT_INLINE_DATA_SIZE) {
        memcpy(event->data.inline_data, data, data_len);
        event->flags |= EVENT_FLAG_DATA_INLINE;
        return event;
    }

    /* 大数据：从 slab 分配 */
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
    struct k_mem_slab* data_slab = event_memory_select_data_slab(data_len);
    if (k_mem_slab_alloc(data_slab, &event->data.ptr, K_NO_WAIT) != 0) {
        notify_slab_exhausted(priority, data_len);
        event_free(event);
        LOG_WRN("Data slab exhausted for size %zu", data_len);
        return NULL;
    }
    memcpy(event->data.ptr, data, data_len);
    event->flags |= EVENT_FLAG_DATA_DYNAMIC;
    return event;
#else
    event_free(event);
    return NULL;
#endif
}
```

- [ ] **Step 4: 实现 event_publish_copy_rt**

```c
event_status_t event_publish_copy_rt(event_type_t type, event_priority_t priority,
                                      const void* data, size_t data_len) {
    event_t* event = event_create_with_data_rt(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);

    /* 发布成功后，数据所有权已转移到队列副本 */
    if (status == EVENT_OK) {
        /* 清除动态数据标志，避免 event_free 释放数据 */
        event->flags &= ~EVENT_FLAG_DATA_DYNAMIC;
    }

    event_free(event);
    return status;
}
```

- [ ] **Step 5: 添加 event_memory.h 头文件引用**

在 event_system.c 顶部添加：

```c
#include "event_memory.h"
```

- [ ] **Step 6: Commit**

```bash
git add src/core/event_system.h src/core/event_system.c
git commit -m "$(cat <<'EOF'
feat(event): implement real-time safe memory allocation APIs

- event_create_rt: O(1) slab allocation, no fallback
- event_create_with_data_rt: inline small data, slab large data
- event_publish_copy_rt: real-time safe publish with copy
- event_create_from_isr: inline wrapper for ISR context

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: 修改现有 API 支持新的内存模型

**Files:**
- Modify: `src/core/event_system.c`

- [ ] **Step 1: 修改 event_create 支持回退**

修改现有的 `event_create` 函数，使其优先尝试 slab，失败时回退 k_malloc：

```c
event_t* event_create(event_type_t type, event_priority_t priority) {
    /* 优先尝试实时安全路径 */
    event_t* event = event_create_rt(type, priority);
    if (event != NULL) {
        return event;
    }

    /* 回退 k_malloc */
#if defined(CONFIG_EVENT_RUNTIME_STATUS) && CONFIG_EVENT_RUNTIME_STATUS
    atomic_inc(&g_fallback_count);
#endif

    event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        LOG_ERR("k_malloc failed for event_t");
        return NULL;
    }

    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = 0;
    event->flags = 0;  /* 非 FROM_SLAB */
    event->reserved = 0;
    memset(event->data.inline_data, 0, CONFIG_EVENT_INLINE_DATA_SIZE);

    return event;
}
```

- [ ] **Step 2: 修改 event_create_with_data 支持内联和回退**

```c
event_t* event_create_with_data(event_type_t type, event_priority_t priority,
                                 const void* data, size_t data_len) {
    if (data == NULL || data_len == 0) {
        return event_create(type, priority);
    }

    /* 验证数据长度 */
    if (data_len > 65535) {
        LOG_ERR("Event data length %zu exceeds maximum 64KB", data_len);
        return NULL;
    }

    /* 小数据：尝试内联 */
    if (data_len <= CONFIG_EVENT_INLINE_DATA_SIZE) {
        event_t* event = event_create(type, priority);
        if (event == NULL) {
            return NULL;
        }
        event->data_len = (uint32_t)data_len;
        memcpy(event->data.inline_data, data, data_len);
        event->flags |= EVENT_FLAG_DATA_INLINE;
        return event;
    }

    /* 大数据：尝试 slab */
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE
    struct k_mem_slab* data_slab = event_memory_select_data_slab(data_len);
    if (data_slab != NULL) {
        event_t* event = event_create(type, priority);
        if (event != NULL) {
            if (k_mem_slab_alloc(data_slab, &event->data.ptr, K_NO_WAIT) == 0) {
                memcpy(event->data.ptr, data, data_len);
                event->data_len = (uint32_t)data_len;
                event->flags |= EVENT_FLAG_DATA_DYNAMIC;
                return event;
            }
            /* slab 满，继续回退 */
            event_free(event);
        }
    }
#endif

    /* 回退 k_malloc */
#if defined(CONFIG_EVENT_RUNTIME_STATUS) && CONFIG_EVENT_RUNTIME_STATUS
    atomic_inc(&g_fallback_count);
#endif

    event_t* event = k_malloc(sizeof(event_t));
    if (event == NULL) {
        return NULL;
    }
    event->data.ptr = k_malloc(data_len);
    if (event->data.ptr == NULL) {
        k_free(event);
        return NULL;
    }

    event->type = type;
    event->priority = priority;
    event->timestamp = k_uptime_get_32();
    event->source_id = 0;
    event->data_len = (uint32_t)data_len;
    event->flags = EVENT_FLAG_DATA_DYNAMIC;  /* 非 FROM_SLAB */
    event->reserved = 0;
    memcpy(event->data.ptr, data, data_len);

    return event;
}
```

- [ ] **Step 3: 修改 event_free 支持新的内存模型**

```c
void event_free(event_t* event) {
    if (event == NULL) {
        return;
    }

    /* 释放动态数据 */
    if (event->flags & EVENT_FLAG_DATA_DYNAMIC) {
        k_free(event->data.ptr);
        event->flags &= ~EVENT_FLAG_DATA_DYNAMIC;
    }

    /* 释放 event_t */
    if (event->flags & EVENT_FLAG_FROM_SLAB) {
#if EVENT_SLAB_ENABLED
        struct k_mem_slab* slab = event_memory_select_event_slab(event->priority);
        k_mem_slab_free(slab, (void*)event);
#endif
    } else {
        k_free(event);
    }
}
```

- [ ] **Step 4: 修改 event_publish_copy 支持新的内存模型**

```c
event_status_t event_publish_copy(event_type_t type, event_priority_t priority,
                                   const void* data, size_t data_len) {
    event_t* event = event_create_with_data(type, priority, data, data_len);
    if (event == NULL) {
        return EVENT_ERR_NO_MEM;
    }

    event_status_t status = event_publish(event);

    /* 发布成功后，数据所有权已转移到队列副本 */
    if (status == EVENT_OK) {
        event->flags &= ~EVENT_FLAG_DATA_DYNAMIC;
    }

    event_free(event);
    return status;
}
```

- [ ] **Step 5: 更新代码中对 is_dynamic 的引用**

搜索并替换所有 `event->is_dynamic` 为 `(event->flags & EVENT_FLAG_DATA_DYNAMIC)`。

- [ ] **Step 6: Commit**

```bash
git add src/core/event_system.c
git commit -m "$(cat <<'EOF'
refactor(event): update existing APIs for new memory model

- event_create: try slab first, fallback to k_malloc
- event_create_with_data: support inline data and slab
- event_free: handle inline/slab/k_malloc sources
- event_publish_copy: updated for new memory model
- Replace is_dynamic with flags & EVENT_FLAG_DATA_DYNAMIC

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: 更新 CMakeLists.txt

**Files:**
- Modify: `src/core/CMakeLists.txt`（如果存在）或项目根目录的构建文件

- [ ] **Step 1: 添加 event_memory.c 到构建**

如果 `src/core/CMakeLists.txt` 存在，添加：
```cmake
zephyr_sources(event_memory.c)
```

如果使用其他构建方式，确保 `event_memory.c` 被编译。

- [ ] **Step 2: Commit**

```bash
git add src/core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
build: add event_memory.c to build

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: 更新其他模块对 event_t 的引用

**Files:**
- Modify: `src/core/event_dispatcher.c`
- Modify: `src/core/event_queue.c`
- Modify: `src/core/event_system_compat.c`

- [ ] **Step 1: 搜索所有 is_dynamic 引用**

```bash
grep -r "is_dynamic" src/core/
```

- [ ] **Step 2: 替换 event_dispatcher.c 中的 is_dynamic**

将 `event->is_dynamic` 替换为 `(event->flags & EVENT_FLAG_DATA_DYNAMIC)`。

- [ ] **Step 3: 替换 event_queue.c 中的 is_dynamic**

同样替换为标志位检查。

- [ ] **Step 4: 替换 event_system_compat.c 中的 is_dynamic**

同样替换为标志位检查。

- [ ] **Step 5: Commit**

```bash
git add src/core/event_dispatcher.c src/core/event_queue.c src/core/event_system_compat.c
git commit -m "$(cat <<'EOF'
refactor(event): replace is_dynamic with flags in all modules

Update event_dispatcher, event_queue, and event_system_compat
to use EVENT_FLAG_DATA_DYNAMIC instead of is_dynamic field.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: 验证编译和测试

- [ ] **Step 1: 编译项目**

```bash
cd D:/Code/3-Project/zephyr_template
west build -b native_posix
```

Expected: 编译成功，无错误

- [ ] **Step 2: 运行测试（如果有）**

```bash
west build -b native_posix -t run
```

- [ ] **Step 3: 修复任何编译错误**

如果出现编译错误，根据错误信息修复。

- [ ] **Step 4: Final Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
feat(event): complete slab memory management implementation

- Real-time safe O(1) allocation via k_mem_slab
- Priority-based memory isolation
- Inline data optimization for small payloads
- Fallback to k_malloc for flexible mode
- Configurable via Kconfig for all device sizes

Memory usage (default config):
- CRITICAL: 8 events × 64B = 512B
- HIGH: 16 events × 64B = 1KB
- NORMAL: 32 events × 64B = 2KB
- Large data: 256B×8 + 1KB×4 + 4KB×2 = 14KB
- Total: ~17.5KB

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## 规范覆盖检查

| 规范要求 | 任务覆盖 |
|----------|----------|
| event_t 结构体重构 | Task 2 |
| Slab 池配置 (Kconfig) | Task 1 |
| Slab 池定义 | Task 3 |
| event_create_rt | Task 4 |
| event_create_with_data_rt | Task 4 |
| event_create (灵活模式) | Task 5 |
| event_create_with_data (灵活模式) | Task 5 |
| event_free | Task 5 |
| event_publish_copy_rt | Task 4 |
| event_publish_copy | Task 5 |
| event_create_from_isr | Task 4 |
| event_slab_available | Task 3 |
| event_slab_remaining | Task 3 |
| event_get_slab_stats | Task 3 |
| Slab 耗尽回调 | Task 3 |
| 内存调试支持 | Task 3 |
| 编译时验证 | Task 2, Task 3 |
