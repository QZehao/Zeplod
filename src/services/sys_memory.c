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
#include <string.h>
#include <limits.h>

LOG_MODULE_REGISTER(sys_memory, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#ifndef CONFIG_SYS_MEMORY_POOL_SIZE
#define CONFIG_SYS_MEMORY_POOL_SIZE 8192
#endif

#define DEFAULT_POOL_SIZE     CONFIG_SYS_MEMORY_POOL_SIZE
#define MAX_ALLOCATIONS       256
#define MEMORY_MAGIC          0x4D454D30U /* "MEM0" */
#define MEMORY_FREED_MAGIC    0x46524545U /* "FREE" */
#define MEMORY_ALIGN_BYTES    4U

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    uint32_t magic;
    uint32_t pool_type;
    size_t requested_size;
    size_t payload_size;
} mem_alloc_header_t;

typedef struct {
    uint8_t *buffer;
    size_t total_size;
    size_t used_size;
    size_t max_used;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
    sys_mem_pool_type_t type;
    struct k_mutex lock;
    bool initialized;
} mem_pool_t;

typedef struct {
    sys_mem_alloc_info_t allocations[MAX_ALLOCATIONS];
    uint32_t count;
    uint32_t max_records;
    bool tracking_enabled;
    struct k_mutex lock;
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

static bool align_up_size(size_t size, size_t align, size_t *aligned)
{
    if (aligned == NULL || align == 0U) {
        return false;
    }

    if (size > (SIZE_MAX - (align - 1U))) {
        return false;
    }

    *aligned = (size + align - 1U) & ~(align - 1U);
    return true;
}

static mem_alloc_header_t *get_alloc_header(void *ptr)
{
    if (ptr == NULL) {
        return NULL;
    }

    return (mem_alloc_header_t *)((uint8_t *)ptr - sizeof(mem_alloc_header_t));
}

static bool ptr_in_pool(const mem_pool_t *pool, const void *ptr)
{
    if (pool == NULL || ptr == NULL || !pool->initialized) {
        return false;
    }

    uintptr_t start = (uintptr_t)pool->buffer;
    uintptr_t end = start + pool->total_size;
    uintptr_t addr = (uintptr_t)ptr;

    return (addr >= start && addr < end);
}

static mem_pool_t *find_pool_containing_ptr(const void *ptr)
{
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        mem_pool_t *pool = &g_sys_mem.pools[i];
        if (ptr_in_pool(pool, ptr)) {
            return pool;
        }
    }

    return NULL;
}

static mem_pool_t *get_pool_from_ptr(void *ptr, mem_alloc_header_t **header_out)
{
    mem_pool_t *pool = find_pool_containing_ptr(ptr);
    if (pool == NULL) {
        return NULL;
    }

    mem_alloc_header_t *header = get_alloc_header(ptr);
    if (header == NULL || !ptr_in_pool(pool, header)) {
        return NULL;
    }

    if ((header->magic != MEMORY_MAGIC && header->magic != MEMORY_FREED_MAGIC) ||
        header->pool_type >= SYS_MEM_POOL_COUNT ||
        header->pool_type != (uint32_t)pool->type) {
        return NULL;
    }

    if (header_out != NULL) {
        *header_out = header;
    }

    return pool;
}

static void tracker_add(void *ptr, size_t size)
{
    if (!g_sys_mem.tracker.tracking_enabled || ptr == NULL) {
        return;
    }

    k_mutex_lock(&g_sys_mem.tracker.lock, K_FOREVER);

    if (g_sys_mem.tracker.count < g_sys_mem.tracker.max_records) {
        sys_mem_alloc_info_t *info = &g_sys_mem.tracker.allocations[g_sys_mem.tracker.count];
        info->ptr = ptr;
        info->size = size;
        info->timestamp = k_uptime_get_32();
        info->module = NULL;
        info->line = 0U;
        g_sys_mem.tracker.count++;
    }

    k_mutex_unlock(&g_sys_mem.tracker.lock);
}

static void tracker_remove(void *ptr)
{
    if (!g_sys_mem.tracker.tracking_enabled || ptr == NULL) {
        return;
    }

    k_mutex_lock(&g_sys_mem.tracker.lock, K_FOREVER);

    for (uint32_t i = 0; i < g_sys_mem.tracker.count; i++) {
        if (g_sys_mem.tracker.allocations[i].ptr == ptr) {
            memmove(&g_sys_mem.tracker.allocations[i],
                    &g_sys_mem.tracker.allocations[i + 1],
                    (g_sys_mem.tracker.count - i - 1U) * sizeof(sys_mem_alloc_info_t));
            g_sys_mem.tracker.count--;
            break;
        }
    }

    k_mutex_unlock(&g_sys_mem.tracker.lock);
}

static void *pool_alloc(mem_pool_t *pool, size_t size, bool zero)
{
    if (pool == NULL || !pool->initialized) {
        return NULL;
    }

    size_t payload_size;
    if (!align_up_size(size, MEMORY_ALIGN_BYTES, &payload_size)) {
        k_mutex_lock(&pool->lock, K_FOREVER);
        pool->fail_count++;
        k_mutex_unlock(&pool->lock);
        return NULL;
    }

    if (payload_size > (SIZE_MAX - sizeof(mem_alloc_header_t))) {
        k_mutex_lock(&pool->lock, K_FOREVER);
        pool->fail_count++;
        k_mutex_unlock(&pool->lock);
        return NULL;
    }

    size_t total_alloc_size = sizeof(mem_alloc_header_t) + payload_size;

    k_mutex_lock(&pool->lock, K_FOREVER);

    if (pool->used_size > pool->total_size ||
        total_alloc_size > (pool->total_size - pool->used_size)) {
        pool->fail_count++;
        k_mutex_unlock(&pool->lock);
        return NULL;
    }

    uint8_t *raw_ptr = pool->buffer + pool->used_size;
    mem_alloc_header_t *header = (mem_alloc_header_t *)raw_ptr;
    void *user_ptr = raw_ptr + sizeof(mem_alloc_header_t);

    header->magic = MEMORY_MAGIC;
    header->pool_type = (uint32_t)pool->type;
    header->requested_size = size;
    header->payload_size = payload_size;

    if (zero) {
        memset(user_ptr, 0, payload_size);
    }

    pool->used_size += total_alloc_size;
    pool->alloc_count++;

    if (pool->used_size > pool->max_used) {
        pool->max_used = pool->used_size;
    }

    k_mutex_unlock(&pool->lock);

    tracker_add(user_ptr, size);
    return user_ptr;
}

static void pool_free(mem_pool_t *expected_pool, void *ptr)
{
    mem_alloc_header_t *header;
    mem_pool_t *actual_pool = get_pool_from_ptr(ptr, &header);

    if (actual_pool == NULL || header == NULL) {
        LOG_WRN("Ignoring invalid memory free: ptr=%p", ptr);
        return;
    }

    if (expected_pool != NULL && expected_pool != actual_pool) {
        LOG_WRN("Free pool mismatch: expected=%d actual=%d ptr=%p",
                expected_pool->type, actual_pool->type, ptr);
    }

    k_mutex_lock(&actual_pool->lock, K_FOREVER);

    if (header->magic == MEMORY_FREED_MAGIC) {
        k_mutex_unlock(&actual_pool->lock);
        LOG_WRN("Double free detected: ptr=%p", ptr);
        return;
    }

    if (header->magic != MEMORY_MAGIC) {
        k_mutex_unlock(&actual_pool->lock);
        LOG_WRN("Corrupted allocation header: ptr=%p", ptr);
        return;
    }

    header->magic = MEMORY_FREED_MAGIC;

    if (actual_pool->free_count < actual_pool->alloc_count) {
        actual_pool->free_count++;
    }

    k_mutex_unlock(&actual_pool->lock);

    tracker_remove(ptr);
}

static size_t get_allocation_size(void *ptr)
{
    mem_alloc_header_t *header;
    mem_pool_t *pool = get_pool_from_ptr(ptr, &header);

    if (pool == NULL || header == NULL) {
        return 0U;
    }

    return header->requested_size;
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
        size_t configured_size = g_sys_mem.config.pool_sizes[i];

        if (configured_size > DEFAULT_POOL_SIZE) {
            LOG_WRN("Pool %d size %u exceeds buffer %u, clamped",
                    i, (uint32_t)configured_size, (uint32_t)DEFAULT_POOL_SIZE);
            configured_size = DEFAULT_POOL_SIZE;
            g_sys_mem.config.pool_sizes[i] = DEFAULT_POOL_SIZE;
        }

        pool->buffer = g_mem_buffer[i];
        pool->total_size = configured_size;
        pool->used_size = 0;
        pool->max_used = 0;
        pool->alloc_count = 0;
        pool->free_count = 0;
        pool->fail_count = 0;
        pool->type = (sys_mem_pool_type_t)i;
        pool->initialized = (pool->total_size > 0U);
        k_mutex_init(&pool->lock);

        if (pool->initialized) {
            LOG_DBG("Pool %d initialized: %u bytes", i, (uint32_t)pool->total_size);
        }
    }

    /* Initialize tracker */
    g_sys_mem.tracker.count = 0;
    g_sys_mem.tracker.max_records = g_sys_mem.config.max_allocations;
    if (g_sys_mem.tracker.max_records == 0U || g_sys_mem.tracker.max_records > MAX_ALLOCATIONS) {
        g_sys_mem.tracker.max_records = MAX_ALLOCATIONS;
    }
    g_sys_mem.tracker.tracking_enabled = g_sys_mem.config.enable_tracking;
    k_mutex_init(&g_sys_mem.tracker.lock);

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
    if (pool != NULL && !pool->initialized) {
        pool = NULL;
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

    size_t old_size = get_allocation_size(ptr);
    if (old_size == 0U) {
        LOG_WRN("realloc on invalid pointer: %p", ptr);
        return NULL;
    }

    void *new_ptr = sys_mem_alloc(type, size);
    if (new_ptr != NULL) {
        size_t copy_size = (old_size < size) ? old_size : size;
        memcpy(new_ptr, ptr, copy_size);
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

    uint32_t active;

    k_mutex_lock(&pool->lock, K_FOREVER);
    active = (pool->alloc_count >= pool->free_count) ?
             (pool->alloc_count - pool->free_count) : 0U;
    k_mutex_unlock(&pool->lock);

    return active;
}

void sys_mem_dump_allocations(sys_mem_pool_type_t type)
{
    mem_pool_t *pool = get_pool(type);
    if (pool == NULL) {
        return;
    }

    size_t total_size;
    size_t used_size;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;

    k_mutex_lock(&pool->lock, K_FOREVER);
    total_size = pool->total_size;
    used_size = pool->used_size;
    alloc_count = pool->alloc_count;
    free_count = pool->free_count;
    fail_count = pool->fail_count;
    k_mutex_unlock(&pool->lock);

    printk("\n=== Memory Pool %d Dump ===\n", type);
    printk("Total: %u, Used: %u, Free: %u\n",
           (uint32_t)total_size,
           (uint32_t)used_size,
           (uint32_t)(total_size - used_size));
    printk("Allocations: %u, Frees: %u, Fails: %u\n",
           alloc_count, free_count, fail_count);

    if (g_sys_mem.tracker.tracking_enabled) {
        k_mutex_lock(&g_sys_mem.tracker.lock, K_FOREVER);

        if (g_sys_mem.tracker.count > 0U) {
            printk("\nActive Allocations:\n");
            for (uint32_t i = 0; i < g_sys_mem.tracker.count; i++) {
                sys_mem_alloc_info_t *info = &g_sys_mem.tracker.allocations[i];
                mem_alloc_header_t *header = get_alloc_header(info->ptr);

                if (header->magic == MEMORY_MAGIC && header->pool_type == (uint32_t)type) {
                    printk("  [%u] ptr=%p, size=%u, time=%u\n",
                           i, info->ptr, (uint32_t)info->size, info->timestamp);
                }
            }
        }

        k_mutex_unlock(&g_sys_mem.tracker.lock);
    }

    printk("=== End Dump ===\n\n");
}

uint32_t sys_mem_check_leaks(sys_mem_pool_type_t type)
{
    if (!g_sys_mem.tracker.tracking_enabled) {
        return 0U;
    }

    uint32_t leaks = 0U;

    k_mutex_lock(&g_sys_mem.tracker.lock, K_FOREVER);

    if (type >= SYS_MEM_POOL_COUNT) {
        leaks = g_sys_mem.tracker.count;
    } else {
        for (uint32_t i = 0; i < g_sys_mem.tracker.count; i++) {
            mem_alloc_header_t *header = get_alloc_header(g_sys_mem.tracker.allocations[i].ptr);
            if (header->magic == MEMORY_MAGIC && header->pool_type == (uint32_t)type) {
                leaks++;
            }
        }
    }

    k_mutex_unlock(&g_sys_mem.tracker.lock);

    return leaks;
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
        mem_pool_t *pool = &g_sys_mem.pools[i];
        if (!pool->initialized) {
            continue;
        }

        k_mutex_lock(&pool->lock, K_FOREVER);
        total += pool->total_size;
        k_mutex_unlock(&pool->lock);
    }
    return total;
}

size_t sys_mem_get_free_size(void)
{
    size_t free_size = 0;
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        mem_pool_t *pool = &g_sys_mem.pools[i];
        if (!pool->initialized) {
            continue;
        }

        k_mutex_lock(&pool->lock, K_FOREVER);
        free_size += pool->total_size - pool->used_size;
        k_mutex_unlock(&pool->lock);
    }
    return free_size;
}

size_t sys_mem_get_min_free_size(void)
{
    size_t min_free = SIZE_MAX;
    for (int i = 0; i < SYS_MEM_POOL_COUNT; i++) {
        mem_pool_t *pool = &g_sys_mem.pools[i];
        if (!pool->initialized) {
            continue;
        }

        k_mutex_lock(&pool->lock, K_FOREVER);
        size_t free_size = pool->total_size - pool->max_used;
        k_mutex_unlock(&pool->lock);

        if (free_size < min_free) {
            min_free = free_size;
        }
    }
    return (min_free == SIZE_MAX) ? 0 : min_free;
}
