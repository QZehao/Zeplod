/**
 * @file data_bus_memory.c
 * @brief Data Bus memory management - slab pools + reference counting lifecycle
 */

#include "data_bus_memory.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <string.h>

/* ============================================================================
 * Block struct slab (always enabled)
 * ============================================================================ */

K_MEM_SLAB_DEFINE(data_bus_block_slab, sizeof(data_bus_block_t),
                  CONFIG_DATA_BUS_MAX_BLOCKS, sizeof(void *));

/* ============================================================================
 * Data buffer slabs (conditional)
 * ============================================================================ */

#if CONFIG_DATA_BUS_SLAB_ENABLE

K_MEM_SLAB_DEFINE(data_bus_slab_256, 256,
                  CONFIG_DATA_BUS_SLAB_256_COUNT, sizeof(void *));
K_MEM_SLAB_DEFINE(data_bus_slab_1k, 1024,
                  CONFIG_DATA_BUS_SLAB_1K_COUNT, sizeof(void *));
K_MEM_SLAB_DEFINE(data_bus_slab_4k, 4096,
                  CONFIG_DATA_BUS_SLAB_4K_COUNT, sizeof(void *));

#endif /* CONFIG_DATA_BUS_SLAB_ENABLE */

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

static struct k_mem_slab *slab_for_size(size_t len)
{
#if CONFIG_DATA_BUS_SLAB_ENABLE
	if (len <= 256) {
		return &data_bus_slab_256;
	} else if (len <= 1024) {
		return &data_bus_slab_1k;
	} else if (len <= 4096) {
		return &data_bus_slab_4k;
	}
#endif
	return NULL;
}

static void *alloc_from_slab(struct k_mem_slab *slab, k_timeout_t timeout)
{
	void *ptr = NULL;
	int ret = k_mem_slab_alloc(slab, &ptr, timeout);
	return (ret == 0) ? ptr : NULL;
}

static void free_data_buf(void *ptr, struct k_mem_slab *slab)
{
	if (slab != NULL) {
		k_mem_slab_free(slab, ptr);
	} else {
		k_free(ptr);
	}
}

/* ============================================================================
 * Two-step allocation
 * ============================================================================ */

static data_bus_block_t *mem_alloc_impl(size_t len, bool isr)
{
	data_bus_block_t *block = NULL;
	void *data_ptr = NULL;
	struct k_mem_slab *slab = NULL;

	/* ---- Step 1: allocate block struct from slab ---- */
	int ret = k_mem_slab_alloc(&data_bus_block_slab, (void **)&block, K_NO_WAIT);
	if (ret != 0) {
		return NULL;
	}

	/* ---- Step 2: allocate data buffer ---- */
	slab = slab_for_size(len);
	if (slab != NULL) {
		/* Slab path */
		data_ptr = alloc_from_slab(slab, isr ? K_NO_WAIT : K_FOREVER);
	}

	if (data_ptr == NULL && !isr) {
		/* Thread path: k_malloc fallback (only if slab disabled or exhausted) */
		data_ptr = k_malloc(len);
		slab = NULL; /* mark as heap-allocated */
	}

	if (data_ptr == NULL) {
		/* Rollback: free block struct and fail */
		k_mem_slab_free(&data_bus_block_slab, block);
		return NULL;
	}

	/* ---- Initialize block ---- */
	memset(block, 0, sizeof(*block));
	block->ptr = data_ptr;
	block->len = len;
	block->slab = slab;
	atomic_set(&block->ref_count, 0);

	return block;
}

data_bus_block_t *data_bus_mem_alloc(size_t len)
{
	return mem_alloc_impl(len, false);
}

data_bus_block_t *data_bus_mem_alloc_isr(size_t len)
{
	return mem_alloc_impl(len, true);
}

/* ============================================================================
 * Direct free (for rollback before entering ref-count lifecycle)
 * ============================================================================ */

void data_bus_mem_free(data_bus_block_t *block)
{
	if (block == NULL) {
		return;
	}

	__ASSERT(atomic_get(&block->ref_count) == 0,
		 "data_bus_mem_free called on block with ref_count != 0");

	/* Free data buffer */
	if (block->ptr != NULL) {
		free_data_buf(block->ptr, block->slab);
	}

	/* Free block struct (always from slab) */
	k_mem_slab_free(&data_bus_block_slab, block);
}

/* ============================================================================
 * Reference counting (public API)
 * ============================================================================ */

void data_bus_block_acquire(data_bus_block_t *block)
{
	if (block == NULL) {
		return;
	}
	atomic_inc(&block->ref_count);
}

void data_bus_block_release(data_bus_block_t *block)
{
	if (block == NULL) {
		return;
	}

	atomic_val_t prev = atomic_dec(&block->ref_count);

	if (prev == 0) {
		/* Release on ref_count 0 would underflow; restore and bail */
		(void)atomic_inc(&block->ref_count);
#if IS_ENABLED(CONFIG_DATA_BUS_DEBUG_REFCNT)
		__ASSERT(0, "data_bus_block_release: ref_count underflow");
#endif
		return;
	}

	if (prev == 1) {
		/* Last reference: free data buffer and block struct */
		if (block->ptr != NULL) {
			free_data_buf(block->ptr, block->slab);
		}
		k_mem_slab_free(&data_bus_block_slab, block);
	}
}

data_bus_block_t *data_bus_block_retain(data_bus_block_t *block)
{
	if (block == NULL) {
		return NULL;
	}
	data_bus_block_acquire(block);
	return block;
}
