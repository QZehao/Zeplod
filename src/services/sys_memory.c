/**
 * @file sys_memory.c
 * @brief System Memory Management Implementation
 * 
 * Memory pool management with allocation tracking and statistics.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "sys_memory.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/heap.h>
#include <string.h>

LOG_MODULE_REGISTER(sys_memory, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#ifndef CONFIG_SYS_MEMORY_POOL_SIZE
#define CONFIG_SYS_MEMORY_POOL_SIZE 8192
#endif

#define DEFAULT_POOL_SIZE  CONFIG_SYS_MEMORY_POOL_SIZE
#define MAX_ALLOCATIONS    256
#define MEMORY_MAGIC       0xMEM0

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    uint8_t *buffer;
    size_t total_size;
    size_t used_size;
    size_t max_used;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
    struct k_mutex lock;
    bool initialized;
} mem_pool_t;

typedef struct {
    sys_mem_alloc_info_t allocations[MAX_ALLOCATIONS];
    uint32_t count;
    bool tracking_enabled;
} mem_tracker_t;

typedef struct {
    mem_pool_t pools[SYS_MEM_POOL_COUNT];
    mem_tracker_t tracker;
    sys_mem_config_t config;
} sys_mem_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static sys_mem_cb_t g_sys_mem;
static uint8_t g_mem_buffer[SYS_MEM_POOL_COUNT][DEFAULT_POOL_SIZE];

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static mem_pool_t *get_pool(sys_mem_pool_type_t type)
{
    if (type >= SYS_MEM_POOL_COUNT) {
        return NULL;
    }
    return &g_sys_mem.pools[type];
}

static void *pool_alloc(mem_pool_t *pool, size_t size, bool zero)
{
    if (pool == NULL || !pool->initialized) {
        return NULL;
    }

    /* Align size to 4 bytes */
    size_t aligned_size = (size + 3) & ~3;

    k_mutex_lock(&pool->lock, K_FOREVER);

    /* Simple bump allocator for demonstration */
    /* In production, use a proper memory pool algorithm */
    if (pool->used_size + aligned_size > pool->total_size) {
        pool->fail_count++;
        k_mutex_unlock(&pool->lock);
        return NULL;
    }

    void *ptr = pool->buffer + pool->used_size;
    
    if (zero) {
        memset(ptr, 0, aligned_size);
    }

    pool->used_size += aligned_size;
    pool->alloc_count++;

    if (pool->used_size > pool->max_used) {
        pool->max_used = pool->used_size;
    }

    /* Track allocation if enabled */
    if (g_sys_mem.tracker.tracking_enabled && 
        g_sys_mem.tracker.count < MAX_ALLOCATIONS) {
        sys_mem_alloc_info_t *info = &g_sys_mem.tracker.allocations[g_sys_mem.tracker.count];
        info->ptr = ptr;
        info->size = size;
        info->timestamp = k_uptime_get_32();
        g_sys_mem.tracker.count++;
    }

    k_mutex_unlock(&pool->lock);
    return ptr;
}

static void pool_free(mem_pool_t *pool, void *ptr)
{
    if (pool == NULL || !pool->initialized || ptr == NULL) {
        return;
    }

    k_mutex_lock(&pool->lock, K_FOREVER);

    /* Simple implementation - just increment free count */
    /* In production, implement proper free list management */
    pool->free_count++;

    /* Remove from tracker */
    if (g_sys_mem.tracker.tracking_enabled) {
        for (uint32_t i = 0; i < g_sys_mem.tracker.count; i++) {
            if (g_sys_mem.tracker.allocations[i].ptr == ptr) {
                /* Shift remaining entries */
                memmove(&g_sys_mem.tracker.allocations[i],
                       &g_sys_mem.tracker.allocations[i + 1],
                       (g_sys_mem.tracker.count - i - 1) * sizeof(sys_mem_alloc_info_t));
                g_sys_mem.tracker.count--;
                break;
            }
        }
    }

    k_mutex_unlock(&pool->lock);
}

/* =============================================================================
 * Core API Implementation
 * ============================================================================= */

int sys_mem_init(const sys_mem_config_t *config)
{
    LOG_INF("Initializing memory system...");

    memset(&g_sys_mem, 0, sizeof(g_sys_mem));

    /* Set config */
    if (config != NULL) {
        g_sys_mem.config = *config;
    } else {
        g_sys_mem.config.pool_sizes[SYS_MEM_POOL_GENERAL] = DEFAULT_POOL_SIZE;
        g_sys_mem.config.pool_sizes[SYS_MEM_POOL_EVENT] = DEFAULT_POOL_SIZE / 2;
        g_sys_mem.config.pool_sizes[SYS_MEM_POOL_MODULE] = DEFAULT_POOL_SIZE / 2;
        g_sys_mem.config.pool_sizes[SYS_MEM_POOL_DMA] = 0;  /* Not enabled by default */
        g_sys_mem.config.enable_tracking = true;
        g_sys_mem.config.enable_defrag = false;
        g_sys_mem.config.max_allocations = MAX_ALLOCATIONS;
    }

    /* Initialize pools */
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        mem_pool_t *pool = &g_sys_mem.pools[i];
        pool->buffer = g_mem_buffer[i];
        pool->total_size = g_sys_mem.config.pool_sizes[i];
        pool->used_size = 0;
        pool->max_used = 0;
        pool->alloc_count = 0;
        pool->free_count = 0;
        pool->fail_count = 0;
        pool->initialized = (pool->total_size > 0);
        k_mutex_init(&pool->lock);

        if (pool->initialized) {
            LOG_DBG("Pool %d initialized: %d bytes", i, pool->total_size);
        }
    }

    /* Initialize tracker */
    g_sys_mem.tracker.count = 0;
    g_sys_mem.tracker.tracking_enabled = g_sys_mem.config.enable_tracking;

    LOG_INF("Memory system initialized");
    return 0;
}

void *sys_mem_alloc(sys_mem_pool_type_t type, size_t size)
{
    if (size == 0) {
        return NULL;
    }

    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        /* Fall back to general pool */
        pool = get_pool(SYS_MEM_POOL_GENERAL);
    }

    return pool_alloc(pool, size, false);
}

void *sys_mem_calloc(sys_mem_pool_type_t type, size_t size)
{
    if (size == 0) {
        return NULL;
    }

    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        pool = get_pool(SYS_MEM_POOL_GENERAL);
    }

    return pool_alloc(pool, size, true);
}

void sys_mem_free(sys_mem_pool_type_t type, void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        pool = get_pool(SYS_MEM_POOL_GENERAL);
    }

    pool_free(pool, ptr);
}

void *sys_mem_realloc(sys_mem_pool_type_t type, void *ptr, size_t size)
{
    if (ptr == NULL) {
        return sys_mem_alloc(type, size);
    }

    if (size == 0) {
        sys_mem_free(type, ptr);
        return NULL;
    }

    /* Allocate new, copy, free old */
    void *new_ptr = sys_mem_alloc(type, size);
    if (new_ptr != NULL) {
        /* In a simple bump allocator, we can't know old size */
        /* Production code should track allocation sizes */
        memcpy(new_ptr, ptr, size);  /* May copy more than needed */
        sys_mem_free(type, ptr);
    }

    return new_ptr;
}

/* =============================================================================
 * Statistics & Debug API
 * ============================================================================= */

void sys_mem_get_stats(sys_mem_pool_type_t type, sys_mem_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        memset(stats, 0, sizeof(sys_mem_stats_t));
        return;
    }

    k_mutex_lock(&pool->lock, K_FOREVER);

    stats->total_size = pool->total_size;
    stats->used_size = pool->used_size;
    stats->free_size = pool->total_size - pool->used_size;
    stats->max_used = pool->max_used;
    stats->alloc_count = pool->alloc_count;
    stats->free_count = pool->free_count;
    stats->fail_count = pool->fail_count;
    
    /* Calculate fragmentation (simplified) */
    if (pool->total_size > 0) {
        stats->fragmentation = (pool->max_used * 100) / pool->total_size;
    }

    k_mutex_unlock(&pool->lock);
}

void sys_mem_reset_stats(sys_mem_pool_type_t type)
{
    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        return;
    }

    k_mutex_lock(&pool->lock, K_FOREVER);
    pool->max_used = pool->used_size;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->fail_count = 0;
    k_mutex_unlock(&pool->lock);
}

uint32_t sys_mem_get_active_allocations(sys_mem_pool_type_t type)
{
    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        return 0;
    }

    return pool->alloc_count - pool->free_count;
}

void sys_mem_dump_allocations(sys_mem_pool_type_t type)
{
    mem_pool_t *pool = get_pool(type);
    if (pool == NULL) {
        return;
    }

    printk("\n=== Memory Pool %d Dump ===\n", type);
    printk("Total: %d, Used: %d, Free: %d\n",
           pool->total_size, pool->used_size, pool->total_size - pool->used_size);
    printk("Allocations: %d, Frees: %d, Fails: %d\n",
           pool->alloc_count, pool->free_count, pool->fail_count);

    if (g_sys_mem.tracker.tracking_enabled && g_sys_mem.tracker.count > 0) {
        printk("\nActive Allocations:\n");
        for (uint32_t i = 0; i < g_sys_mem.tracker.count; i++) {
            sys_mem_alloc_info_t *info = &g_sys_mem.tracker.allocations[i];
            printk("  [%d] ptr=%p, size=%d, time=%d\n",
                   i, info->ptr, info->size, info->timestamp);
        }
    }

    printk("=== End Dump ===\n\n");
}

uint32_t sys_mem_check_leaks(sys_mem_pool_type_t type)
{
    if (!g_sys_mem.tracker.tracking_enabled) {
        return 0;
    }

    return g_sys_mem.tracker.count;
}

size_t sys_mem_defrag(sys_mem_pool_type_t type)
{
    /* Simple implementation - just reset the pool */
    /* Production code should implement proper defragmentation */
    mem_pool_t *pool = get_pool(type);
    if (pool == NULL || !pool->initialized) {
        return 0;
    }

    if (!g_sys_mem.config.enable_defrag) {
        return 0;
    }

    k_mutex_lock(&pool->lock, K_FOREVER);
    
    size_t reclaimed = 0;
    /* In a bump allocator, we can only defrag if all allocations are freed */
    if (pool->alloc_count == pool->free_count) {
        reclaimed = pool->used_size;
        pool->used_size = 0;
    }
    
    k_mutex_unlock(&pool->lock);
    return reclaimed;
}

/* =============================================================================
 * Heap Information
 * ============================================================================= */

size_t sys_mem_get_heap_size(void)
{
    size_t total = 0;
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        if (g_sys_mem.pools[i].initialized) {
            total += g_sys_mem.pools[i].total_size;
        }
    }
    return total;
}

size_t sys_mem_get_free_size(void)
{
    size_t free_size = 0;
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        if (g_sys_mem.pools[i].initialized) {
            free_size += g_sys_mem.pools[i].total_size - g_sys_mem.pools[i].used_size;
        }
    }
    return free_size;
}

size_t sys_mem_get_min_free_size(void)
{
    size_t min_free = SIZE_MAX;
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        if (g_sys_mem.pools[i].initialized) {
            size_t free_size = g_sys_mem.pools[i].total_size - g_sys_mem.pools[i].max_used;
            if (free_size < min_free) {
                min_free = free_size;
            }
        }
    }
    return (min_free == SIZE_MAX) ? 0 : min_free;
}
