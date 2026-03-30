/**
 * @file sys_memory.h
 * @brief System Memory Management Header
 * 
 * Memory pool management with allocation tracking and statistics.
 * 
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Memory pool types
 */
typedef enum {
    SYS_MEM_POOL_GENERAL = 0,   /* General purpose */
    SYS_MEM_POOL_EVENT,         /* Event allocations */
    SYS_MEM_POOL_MODULE,        /* Module-specific */
    SYS_MEM_POOL_DMA,           /* DMA-capable (if available) */
    SYS_MEM_POOL_COUNT
} sys_mem_pool_type_t;

/**
 * @brief Memory statistics
 */
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    size_t max_used;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
    uint32_t fragmentation;
} sys_mem_stats_t;

/**
 * @brief Allocation info for debugging
 */
typedef struct {
    void *ptr;
    size_t size;
    uint32_t timestamp;
    const char *module;
    uint32_t line;
} sys_mem_alloc_info_t;

/* =============================================================================
 * Configuration
 * ============================================================================= */

typedef struct {
    size_t pool_sizes[SYS_MEM_POOL_COUNT];
    bool enable_tracking;
    bool enable_defrag;
    uint32_t max_allocations;
} sys_mem_config_t;

/* =============================================================================
 * Core API
 * ============================================================================= */

/**
 * @brief Initialize memory management system
 * @param config Configuration structure
 * @return 0 on success, negative error code on failure
 */
int sys_mem_init(const sys_mem_config_t *config);

/**
 * @brief Allocate memory from pool
 * @param type Pool type
 * @param size Requested size
 * @return Pointer to allocated memory, NULL on failure
 */
void *sys_mem_alloc(sys_mem_pool_type_t type, size_t size);

/**
 * @brief Allocate zeroed memory from pool
 * @param type Pool type
 * @param size Requested size
 * @return Pointer to allocated memory, NULL on failure
 */
void *sys_mem_calloc(sys_mem_pool_type_t type, size_t size);

/**
 * @brief Free memory back to pool
 * @param type Pool type（用于校验；实际归属以 ptr 的分配头为准）
 * @param ptr Pointer to free
 */
void sys_mem_free(sys_mem_pool_type_t type, void *ptr);

/**
 * @brief Reallocate memory block
 * @param type Pool type
 * @param ptr Existing pointer
 * @param size New size
 * @return Pointer to reallocated memory, NULL on failure
 */
void *sys_mem_realloc(sys_mem_pool_type_t type, void *ptr, size_t size);

/* =============================================================================
 * Statistics & Debug API
 * ============================================================================= */

/**
 * @brief Get memory statistics for a pool
 * @param type Pool type
 * @param stats Output: statistics structure
 */
void sys_mem_get_stats(sys_mem_pool_type_t type, sys_mem_stats_t *stats);

/**
 * @brief Reset memory statistics
 * @param type Pool type
 */
void sys_mem_reset_stats(sys_mem_pool_type_t type);

/**
 * @brief Get number of active allocations
 * @param type Pool type
 * @return Number of active allocations
 */
uint32_t sys_mem_get_active_allocations(sys_mem_pool_type_t type);

/**
 * @brief Dump allocation info (debug)
 * @param type Pool type
 */
void sys_mem_dump_allocations(sys_mem_pool_type_t type);

/**
 * @brief Check for memory leaks
 * @param type Pool type（传入无效值时返回所有池的潜在泄漏总数）
 * @return Number of potential leaks
 */
uint32_t sys_mem_check_leaks(sys_mem_pool_type_t type);

/**
 * @brief Defragment memory pool
 * @param type Pool type
 * @return Bytes reclaimed
 */
size_t sys_mem_defrag(sys_mem_pool_type_t type);

/* =============================================================================
 * Convenience Macros
 * ============================================================================= */

#define SYS_MEM_ALLOC_GENERAL(size) \
    sys_mem_alloc(SYS_MEM_POOL_GENERAL, size)

#define SYS_MEM_FREE_GENERAL(ptr) \
    sys_mem_free(SYS_MEM_POOL_GENERAL, ptr)

#define SYS_MEM_ALLOC_EVENT(size) \
    sys_mem_alloc(SYS_MEM_POOL_EVENT, size)

#define SYS_MEM_FREE_EVENT(ptr) \
    sys_mem_free(SYS_MEM_POOL_EVENT, ptr)

/* =============================================================================
 * Heap Information
 * ============================================================================= */

/**
 * @brief Get total heap size
 * @return Total heap size in bytes
 */
size_t sys_mem_get_heap_size(void);

/**
 * @brief Get free heap size
 * @return Free heap size in bytes
 */
size_t sys_mem_get_free_size(void);

/**
 * @brief Get minimum ever free heap size
 * @return Minimum free heap size in bytes
 */
size_t sys_mem_get_min_free_size(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_MEMORY_H */
