/**
 * @file data_bus_memory.h
 * @brief Data Bus 内部内存管理 API
 *
 * Slab 内存池 + 引用计数生命周期管理。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：添加 data_bus_block_retain()
 *
 */

#ifndef DATA_BUS_MEMORY_H
#define DATA_BUS_MEMORY_H

#include "data_bus.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-context allocation (slab preferred, k_malloc fallback)
 *
 * Two-step allocation:
 *   1. Allocate data_bus_block_t from data_bus_block_slab
 *   2. Allocate data buffer from data slab (or k_malloc)
 *
 * @return Block with ref_count == 0 (not in lifecycle), or NULL on failure
 */
data_bus_block_t* data_bus_mem_alloc(size_t len);

/**
 * @brief ISR-context allocation (slab only, no fallback)
 *
 * Same as above, but step 2 uses slab only (K_NO_WAIT).
 * Returns NULL if slab exhausted or len exceeds max slab size.
 */
data_bus_block_t* data_bus_mem_alloc_isr(size_t len);

/**
 * @brief Direct free (only for blocks not yet in ref-count lifecycle)
 *
 * Frees both struct (slab/k_free) and data buffer (slab/k_free).
 * Used for rollback when publish fails before entering queue.
 */
void data_bus_mem_free(data_bus_block_t* block);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_MEMORY_H */
