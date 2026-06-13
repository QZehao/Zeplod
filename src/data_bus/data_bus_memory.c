/**
 * @file data_bus_memory.c
 * @brief Data Bus 内存管理 - slab 内存池 + 引用计数生命周期
 *
 * 两级分配：block 结构体从固定 slab 分配，数据缓冲区按大小分级 slab + k_malloc 兜底。
 * @author zeh (china_qzh@163.com)
 * @version 2.1
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：添加 data_bus_block_retain()
 * 2026-05-20       2.1            zeh            增加 data_bus_block_ptr / data_bus_block_len
 *
 */

#include "data_bus_memory.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <string.h>
#include "data_bus_internal.h"

LOG_MODULE_REGISTER(data_bus_mem, CONFIG_DATA_BUS_LOG_LEVEL);

/* ============================================================================
 * 块结构体 slab（始终启用）
 * ============================================================================ */

K_MEM_SLAB_DEFINE(data_bus_block_slab, sizeof(data_bus_block_t), CONFIG_DATA_BUS_MAX_BLOCKS, sizeof(void*));

/* ============================================================================
 * 数据缓冲区 slab（条件编译）
 * ============================================================================ */

#if CONFIG_DATA_BUS_SLAB_ENABLE

#if CONFIG_DATA_BUS_SLAB_64_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_64, 64, CONFIG_DATA_BUS_SLAB_64_COUNT, sizeof(void*));
#endif
#if CONFIG_DATA_BUS_SLAB_128_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_128, 128, CONFIG_DATA_BUS_SLAB_128_COUNT, sizeof(void*));
#endif
#if CONFIG_DATA_BUS_SLAB_256_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_256, 256, CONFIG_DATA_BUS_SLAB_256_COUNT, sizeof(void*));
#endif
#if CONFIG_DATA_BUS_SLAB_512_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_512, 512, CONFIG_DATA_BUS_SLAB_512_COUNT, sizeof(void*));
#endif
#if CONFIG_DATA_BUS_SLAB_1K_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_1k, 1024, CONFIG_DATA_BUS_SLAB_1K_COUNT, sizeof(void*));
#endif
#if CONFIG_DATA_BUS_SLAB_4K_COUNT > 0
K_MEM_SLAB_DEFINE(data_bus_slab_4k, 4096, CONFIG_DATA_BUS_SLAB_4K_COUNT, sizeof(void*));
#endif

BUILD_ASSERT(CONFIG_DATA_BUS_MAX_BLOCKS >= CONFIG_DATA_BUS_SLAB_64_COUNT + CONFIG_DATA_BUS_SLAB_128_COUNT +
                                               CONFIG_DATA_BUS_SLAB_256_COUNT + CONFIG_DATA_BUS_SLAB_512_COUNT +
                                               CONFIG_DATA_BUS_SLAB_1K_COUNT + CONFIG_DATA_BUS_SLAB_4K_COUNT,
             "DATA_BUS_MAX_BLOCKS must be >= total data slab blocks");
#endif /* CONFIG_DATA_BUS_SLAB_ENABLE */

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

static struct k_mem_slab* slab_for_size(size_t len) {
#if CONFIG_DATA_BUS_SLAB_ENABLE
#if CONFIG_DATA_BUS_SLAB_64_COUNT > 0
    if (len <= 64) {
        return &data_bus_slab_64;
    }
#endif
#if CONFIG_DATA_BUS_SLAB_128_COUNT > 0
    if (len <= 128) {
        return &data_bus_slab_128;
    }
#endif
#if CONFIG_DATA_BUS_SLAB_256_COUNT > 0
    if (len <= 256) {
        return &data_bus_slab_256;
    }
#endif
#if CONFIG_DATA_BUS_SLAB_512_COUNT > 0
    if (len <= 512) {
        return &data_bus_slab_512;
    }
#endif
#if CONFIG_DATA_BUS_SLAB_1K_COUNT > 0
    if (len <= 1024) {
        return &data_bus_slab_1k;
    }
#endif
#if CONFIG_DATA_BUS_SLAB_4K_COUNT > 0
    if (len <= 4096) {
        return &data_bus_slab_4k;
    }
#endif
#endif
    return NULL;
}

static void* alloc_from_slab(struct k_mem_slab* slab, k_timeout_t timeout) {
    void* ptr = NULL;
    int   ret = k_mem_slab_alloc(slab, &ptr, timeout);
    return (ret == 0) ? ptr : NULL;
}

static void free_data_buf(void* ptr, struct k_mem_slab* slab) {
    if (slab != NULL) {
        k_mem_slab_free(slab, ptr);
    } else {
        k_free(ptr);
    }
}

/* ============================================================================
 * 两步分配
 * ============================================================================ */

static data_bus_block_t* mem_alloc_impl(size_t len, bool isr) {
    data_bus_block_t*  block = NULL;
    void*              data_ptr = NULL;
    struct k_mem_slab* slab = NULL;
    bool               slab_candidate_exhausted = false;
    bool               used_malloc_fallback = false;

    /* ---- 步骤 1：从 slab 分配块结构体 ---- */
    void* mem = NULL;
    int   ret = k_mem_slab_alloc(&data_bus_block_slab, &mem, K_NO_WAIT);
    block = (data_bus_block_t*) mem;
    if (ret != 0) {
        LOG_ERR("Block struct slab exhausted (max=%u)", CONFIG_DATA_BUS_MAX_BLOCKS);
        return NULL;
    }

    /* ---- 步骤 2：分配数据缓冲区 ---- */
    slab = slab_for_size(len);
    if (slab != NULL) {
        /* Slab 路径 */
        data_ptr = alloc_from_slab(slab, K_NO_WAIT);
        slab_candidate_exhausted = (data_ptr == NULL);
    }

    if (data_ptr == NULL && !isr && !IS_ENABLED(CONFIG_DATA_BUS_NO_MALLOC)) {
        /* 线程路径：k_malloc 兜底（slab 禁用或耗尽时；NO_MALLOC 时跳过） */
        LOG_WRN("Data slab exhausted, falling back to k_malloc (len=%zu)", len);
        data_ptr = k_malloc(len);
        used_malloc_fallback = (data_ptr != NULL);
        slab = NULL; /* 标记为堆分配 */
    }

    if (data_ptr == NULL) {
        LOG_ERR("Data buffer allocation failed (len=%zu isr=%d)", len, isr);
        /* 回滚：释放块结构体并失败 */
        k_mem_slab_free(&data_bus_block_slab, block);
        return NULL;
    }

    /* ---- 初始化块 ---- */
    memset(block, 0, sizeof(*block));
    block->ptr = data_ptr;
    block->len = len;
    block->slab = slab;
    block->malloc_fallback = used_malloc_fallback;
    block->slab_exhausted = slab_candidate_exhausted;
    atomic_set(&block->ref_count, 0);

    return block;
}

data_bus_block_t* data_bus_mem_alloc(size_t len) {
    return mem_alloc_impl(len, false);
}

data_bus_block_t* data_bus_mem_alloc_isr(size_t len) {
    return mem_alloc_impl(len, true);
}

/* ============================================================================
 * 直接释放（用于进入引用计数生命周期前的回滚）
 * ============================================================================ */

void data_bus_mem_free(data_bus_block_t* block) {
    if (block == NULL) {
        return;
    }

    __ASSERT(atomic_get(&block->ref_count) == 0, "data_bus_mem_free called on block with ref_count != 0");

    /* 释放数据缓冲区 */
    if (block->ptr != NULL) {
        free_data_buf(block->ptr, block->slab);
    }

    /* 释放块结构体（始终来自 slab） */
    k_mem_slab_free(&data_bus_block_slab, block);
}

/* ============================================================================
 * 引用计数（公共 API）
 * ============================================================================ */

void data_bus_block_acquire(data_bus_block_t* block) {
    if (block == NULL) {
        return;
    }
    atomic_inc(&block->ref_count);
}

void data_bus_block_release(data_bus_block_t* block) {
    if (block == NULL) {
        return;
    }

    atomic_val_t prev = atomic_dec(&block->ref_count);

    if (prev == 0) {
        /* 在 ref_count 0 时释放会导致下溢；恢复并退出 */
        (void) atomic_inc(&block->ref_count);
#if IS_ENABLED(CONFIG_DATA_BUS_DEBUG_REFCNT)
        __ASSERT(0, "data_bus_block_release: ref_count underflow");
#endif
        return;
    }

    if (prev == 1) {
        /* 最后一个引用：释放数据缓冲区和块结构体 */
        LOG_DBG("Block freed (slab=%s)", block->slab ? "y" : "n");
        if (block->ptr != NULL) {
            free_data_buf(block->ptr, block->slab);
        }
        k_mem_slab_free(&data_bus_block_slab, block);
    }
}

data_bus_block_t* data_bus_block_retain(data_bus_block_t* block) {
    if (block == NULL) {
        return NULL;
    }
    data_bus_block_acquire(block);
    return block;
}

void* data_bus_block_ptr(const data_bus_block_t* block) {
    if (block == NULL) {
        return NULL;
    }
    return block->ptr;
}

size_t data_bus_block_len(const data_bus_block_t* block) {
    if (block == NULL) {
        return 0;
    }
    return block->len;
}
