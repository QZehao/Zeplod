/**
 * @file ipc_shared_mem.c
 * @brief IPC 共享内存池与引用计数管理器实现
 *
 * SIL-2 合规实现：
 * - 所有输入参数验证
 * - 原子操作保护引用计数
 * - 互斥锁保护共享状态
 * - 错误码返回检查
 * - 资源泄漏防护
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-13     1.0            zeh            初始版本
 * 2026-05-09     1.1            zeh            active_count 原子化避免与 block 锁逆向嵌套 pool 锁；k_mutex_init
 * 用法修正
 *
 */

#include "ipc_shared_mem.h"

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <string.h>
#include "ipc_service.h"

LOG_MODULE_REGISTER(ipc_shm, CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_LOG_LEVEL);

/* =============================================================================
 * SIL-2: 配置验证宏
 * ============================================================================= */

/** 最大合理的池大小 */
#ifndef IPC_SHM_MAX_POOL_SIZE
#define IPC_SHM_MAX_POOL_SIZE 128U
#endif

/** 最大合理的块大小 */
#ifndef IPC_SHM_MAX_BLOCK_SIZE
#define IPC_SHM_MAX_BLOCK_SIZE 4096U
#endif

/** 最小块大小 */
#ifndef IPC_SHM_MIN_BLOCK_SIZE
#define IPC_SHM_MIN_BLOCK_SIZE 64U
#endif

/* =============================================================================
 * 内部辅助函数
 * ============================================================================= */

/**
 * @brief 句柄校验盐（与池实例地址绑定，降低相邻索引伪造句柄的概率）
 */
static uint32_t shm_handle_salt(const ipc_shm_pool_t* pool) {
    const uintptr_t addr = (uintptr_t) pool;

    return ((uint32_t) (addr >> 4) & 0xFFFFU) ^ 0xA5A5U;
}

/**
 * @brief 从句柄解码块索引
 *
 * 句柄格式：[31:16] = 校验码, [15:0] = 块索引
 * 校验码 = index ^ shm_handle_salt(pool)
 *
 * @param pool 共享内存池
 * @param handle 共享内存句柄
 * @return 块索引，句柄无效返回 UINT32_MAX
 */
static uint32_t decode_block_index(const ipc_shm_pool_t* pool, ipc_shm_handle_t handle) {
    if (handle == IPC_SHM_HANDLE_INVALID || pool == NULL) {
        return UINT32_MAX;
    }

    const uint32_t index = handle & 0xFFFFU;
    const uint32_t checksum = (handle >> 16U) & 0xFFFFU;
    const uint32_t expected_checksum = index ^ shm_handle_salt(pool);

    if (checksum != expected_checksum) {
        LOG_ERR("Invalid handle checksum: handle=0x%08X, expected=0x%04X, got=0x%04X", handle, expected_checksum,
                checksum);
        return UINT32_MAX;
    }

    return index;
}

/**
 * @brief 从块索引编码句柄
 *
 * @param pool 共享内存池
 * @param index 块索引
 * @return 编码后的句柄
 */
static ipc_shm_handle_t encode_handle(const ipc_shm_pool_t* pool, uint32_t index) {
    const uint32_t checksum = index ^ shm_handle_salt(pool);

    return (ipc_shm_handle_t) ((checksum << 16U) | index);
}

/**
 * @brief 获取块指针
 *
 * @param pool 共享内存池
 * @param index 块索引
 * @return 指向实际内存的指针
 */
static void* get_block_ptr(ipc_shm_pool_t* pool, uint32_t index) {
    return &pool->mem_pool[index * CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE];
}

/**
 * @brief 验证块索引有效性
 *
 * SIL-2: 多重验证确保块状态一致
 *
 * @param pool 共享内存池
 * @param index 块索引
 * @return true 有效，false 无效
 */
static bool is_block_valid(const ipc_shm_pool_t* pool, uint32_t index) {
    if (index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        return false;
    }

    const ipc_shm_block_t* block = &pool->blocks[index];

    /* SIL-2: 验证状态一致性 */
    if (block->state == IPC_SHM_STATE_INVALID) {
        return false;
    }

    if (block->state == IPC_SHM_STATE_FREE) {
        /* 空闲块引用计数必须为 0 */
        if (atomic_get(&block->ref_count) != 0) {
            LOG_ERR("Block %u: FREE state but ref_count=%ld", index, (long) atomic_get(&block->ref_count));
            return false;
        }
    } else {
        /* 已分配块引用计数必须 >= 1 */
        if (atomic_get(&block->ref_count) < 1) {
            LOG_ERR("Block %u: ALLOCATED/SHARED state but ref_count=%ld", index, (long) atomic_get(&block->ref_count));
            return false;
        }
    }

    /* 验证指针在池范围内 */
    void*          ptr = block->ptr;
    const uint8_t* pool_start = pool->mem_pool;
    const uint8_t* pool_end = pool_start + sizeof(pool->mem_pool);

    if (ptr < (void*) pool_start || ptr >= (void*) pool_end) {
        LOG_ERR("Block %u: ptr %p out of pool range [%p, %p)", index, ptr, (void*) pool_start, (void*) pool_end);
        return false;
    }

    return true;
}

/* =============================================================================
 * 公共 API 实现
 * ============================================================================= */

/**
 * @brief 初始化共享内存池
 */
int ipc_shm_init(ipc_service_t* service) {
    /* SIL-2: 验证输入参数 */
    if (service == NULL) {
        return -EINVAL;
    }

    /* SIL-2: 验证配置合理性 */
    if (CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE > IPC_SHM_MAX_POOL_SIZE) {
        LOG_ERR("Pool size %u exceeds maximum %u", CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE,
                IPC_SHM_MAX_POOL_SIZE);
        return -EINVAL;
    }

    if (CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE > IPC_SHM_MAX_BLOCK_SIZE ||
        CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE < IPC_SHM_MIN_BLOCK_SIZE) {
        LOG_ERR("Block size %u out of range [%u, %u]", CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE,
                IPC_SHM_MIN_BLOCK_SIZE, IPC_SHM_MAX_BLOCK_SIZE);
        return -EINVAL;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    /* SIL-2: 清零所有状态 */
    memset(pool, 0, sizeof(ipc_shm_pool_t));

    /* SIL-2: 初始化互斥锁（Zephyr k_mutex_init 无返回值） */
    k_mutex_init(&pool->pool_lock);

    /* SIL-2: 初始化所有块 */
    for (uint32_t i = 0; i < CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE; i++) {
        ipc_shm_block_t* block = &pool->blocks[i];

        block->ptr = get_block_ptr(pool, i);
        block->size = CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE;
        atomic_set(&block->ref_count, 0);
        block->state = IPC_SHM_STATE_FREE;
        block->alloc_id = 0;

        k_mutex_init(&block->lock);
    }

    pool->alloc_counter = 0;
    atomic_set(&pool->active_count, 0);
    pool->peak_count = 0;

    LOG_DBG("Shared memory pool initialized: %u blocks x %u bytes", CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE,
            CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE);

    return 0;
}

/**
 * @brief 反初始化共享内存池
 */
void ipc_shm_deinit(ipc_service_t* service) {
    if (service == NULL) {
        return;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    /* SIL-2: 检查是否有未释放的块（active_count 为原子，无需先取 pool_lock） */
    uint32_t leaked = (uint32_t) atomic_get(&pool->active_count);

    if (leaked > 0) {
        LOG_WRN("Shared memory pool has %u leaked blocks on deinit", leaked);

        /* SIL-2: 打印泄漏块信息用于调试 */
        for (uint32_t i = 0; i < CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE; i++) {
            ipc_shm_block_t* block = &pool->blocks[i];
            if (block->state != IPC_SHM_STATE_FREE) {
                LOG_WRN("Leaked block %u: state=%d, ref_count=%ld, alloc_id=%u, size=%u", i, block->state,
                        (long) atomic_get(&block->ref_count), block->alloc_id, block->size);
            }
        }
    }

    LOG_DBG("Shared memory pool deinitialized (leaked=%u, peak=%u)", leaked, pool->peak_count);
}

/**
 * @brief 从池中分配一个共享内存块
 */
ipc_shm_handle_t ipc_shm_alloc(ipc_service_t* service, size_t size) {
    /* SIL-2: 验证输入参数 */
    if (service == NULL) {
        return IPC_SHM_HANDLE_INVALID;
    }

    if (size == 0 || size > CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE) {
        LOG_ERR("Invalid alloc size %u (max %u)", size, CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE);
        return IPC_SHM_HANDLE_INVALID;
    }

    ipc_shm_pool_t*  pool = &service->shm_pool;
    ipc_shm_handle_t handle = IPC_SHM_HANDLE_INVALID;

    k_mutex_lock(&pool->pool_lock, K_FOREVER);

    /* SIL-2: 查找空闲块 */
    for (uint32_t i = 0; i < CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE; i++) {
        ipc_shm_block_t* block = &pool->blocks[i];

        if (block->state == IPC_SHM_STATE_FREE) {
            /* 找到空闲块，分配它 */
            k_mutex_lock(&block->lock, K_FOREVER);

            /* SIL-2: 双重检查状态（防止竞争） */
            if (block->state != IPC_SHM_STATE_FREE) {
                k_mutex_unlock(&block->lock);
                continue;
            }

            /* 初始化块状态 */
            block->size = size;
            atomic_set(&block->ref_count, 1); /* 初始引用计数=1（分配者持有） */
            block->state = IPC_SHM_STATE_ALLOCATED;
            pool->alloc_counter++;
            block->alloc_id = pool->alloc_counter;

            {
                uint32_t prev = (uint32_t) atomic_inc(&pool->active_count);
                uint32_t now = prev + 1U;
                if (now > pool->peak_count) {
                    pool->peak_count = now;
                }
            }

            handle = encode_handle(pool, i);

            LOG_DBG("Allocated block %u: handle=0x%08X, size=%u, alloc_id=%u", i, handle, size, block->alloc_id);

            k_mutex_unlock(&block->lock);
            break;
        }
    }

    k_mutex_unlock(&pool->pool_lock);

    if (handle == IPC_SHM_HANDLE_INVALID) {
        LOG_WRN("Shared memory pool exhausted (active=%u, peak=%u)", (unsigned int) atomic_get(&pool->active_count),
                (unsigned int) pool->peak_count);
    }

    return handle;
}

/**
 * @brief 增加共享内存块的引用计数
 */
void ipc_shm_acquire(ipc_service_t* service, ipc_shm_handle_t handle) {
    /* SIL-2: 验证输入参数 */
    if (service == NULL || handle == IPC_SHM_HANDLE_INVALID) {
        return;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    const uint32_t index = decode_block_index(pool, handle);

    if (index == UINT32_MAX) {
        return; /* 无效句柄，静默忽略 */
    }

    /* SIL-2: 验证索引范围 */
    if (index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        LOG_ERR("Block index %u out of range", index);
        return;
    }

    ipc_shm_block_t* block = &pool->blocks[index];

    /* SIL-2: 使用块锁保护引用计数操作 */
    k_mutex_lock(&block->lock, K_FOREVER);

    /* SIL-2: 验证块状态 */
    if (block->state == IPC_SHM_STATE_FREE || block->state == IPC_SHM_STATE_INVALID) {
        LOG_ERR("Cannot acquire free/invalid block %u", index);
        k_mutex_unlock(&block->lock);
        return;
    }

    /* SIL-2: 原子增加引用计数 */
    uint32_t old_ref = (uint32_t) atomic_inc(&block->ref_count);

    /* 更新状态 */
    if (old_ref == 1) {
        block->state = IPC_SHM_STATE_SHARED;
    }

    LOG_DBG("Acquired block %u: ref_count %u -> %u", index, old_ref, old_ref + 1);

    k_mutex_unlock(&block->lock);
}

/**
 * @brief 减少共享内存块的引用计数
 */
int ipc_shm_release(ipc_service_t* service, ipc_shm_handle_t handle) {
    /* SIL-2: 验证输入参数 */
    if (service == NULL || handle == IPC_SHM_HANDLE_INVALID) {
        return -EINVAL;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    const uint32_t index = decode_block_index(pool, handle);

    if (index == UINT32_MAX) {
        return -EINVAL;
    }

    /* SIL-2: 验证索引范围 */
    if (index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        LOG_ERR("Block index %u out of range", index);
        return -EINVAL;
    }

    ipc_shm_block_t* block = &pool->blocks[index];
    int              ret = 0;

    k_mutex_lock(&block->lock, K_FOREVER);

    /* SIL-2: 验证块状态 */
    if (block->state == IPC_SHM_STATE_FREE || block->state == IPC_SHM_STATE_INVALID) {
        LOG_ERR("Cannot release free/invalid block %u", index);
        ret = -EBUSY;
        goto unlock;
    }

    /* SIL-2: 验证引用计数不会下溢 */
    uint32_t current_ref = (uint32_t) atomic_get(&block->ref_count);
    if (current_ref == 0) {
        LOG_ERR("Block %u: ref_count underflow detected", index);
        block->state = IPC_SHM_STATE_INVALID;
        ret = -EBUSY;
        goto unlock;
    }

    /* SIL-2: 原子减少引用计数 */
    uint32_t old_ref = (uint32_t) atomic_dec(&block->ref_count);
    uint32_t new_ref = old_ref - 1;

    LOG_DBG("Released block %u: ref_count %u -> %u", index, old_ref, new_ref);

    if (new_ref == 0) {
        /* 引用计数降至 0，回收至空闲池 */
        block->state = IPC_SHM_STATE_FREE;
        block->alloc_id = 0;

        /* SIL-2: 清零内存（安全考虑） */
        memset(block->ptr, 0, block->size);
        block->size = CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE;

        /* active_count 原子递减：避免在持有 block->lock 时再取 pool_lock（与 ipc_shm_alloc 锁序冲突） */
        (void) atomic_dec(&pool->active_count);

        LOG_DBG("Block %u recycled (alloc_id=%u)", index, block->alloc_id);
    } else if (new_ref == 1) {
        /* 从共享状态回到分配状态 */
        block->state = IPC_SHM_STATE_ALLOCATED;
    }

unlock:
    k_mutex_unlock(&block->lock);
    return ret;
}

/**
 * @brief 获取共享内存块的实际指针
 */
void* ipc_shm_get_ptr(ipc_service_t* service, ipc_shm_handle_t handle) {
    if (service == NULL || handle == IPC_SHM_HANDLE_INVALID) {
        return NULL;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    const uint32_t index = decode_block_index(pool, handle);

    if (index == UINT32_MAX || index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        return NULL;
    }

    ipc_shm_block_t* block = &pool->blocks[index];
    void*            ptr = NULL;

    k_mutex_lock(&block->lock, K_FOREVER);

    if (block->state != IPC_SHM_STATE_FREE && block->state != IPC_SHM_STATE_INVALID) {
        ptr = block->ptr;
    }

    k_mutex_unlock(&block->lock);
    return ptr;
}

/**
 * @brief 获取共享内存块的大小
 */
size_t ipc_shm_get_size(ipc_service_t* service, ipc_shm_handle_t handle) {
    if (service == NULL || handle == IPC_SHM_HANDLE_INVALID) {
        return 0;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    const uint32_t index = decode_block_index(pool, handle);

    if (index == UINT32_MAX || index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        return 0;
    }

    ipc_shm_block_t* block = &pool->blocks[index];
    size_t           size = 0;

    k_mutex_lock(&block->lock, K_FOREVER);

    if (block->state != IPC_SHM_STATE_FREE && block->state != IPC_SHM_STATE_INVALID) {
        size = block->size;
    }

    k_mutex_unlock(&block->lock);
    return size;
}

/**
 * @brief 验证句柄是否有效且可访问
 */
bool ipc_shm_is_valid(ipc_service_t* service, ipc_shm_handle_t handle) {
    if (service == NULL || handle == IPC_SHM_HANDLE_INVALID) {
        return false;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    const uint32_t index = decode_block_index(pool, handle);

    if (index == UINT32_MAX) {
        return false;
    }

    if (index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        return false;
    }

    return is_block_valid(pool, index);
}

/**
 * @brief 根据池内指针查找共享内存句柄
 */
ipc_shm_handle_t ipc_shm_lookup_handle_by_ptr(ipc_service_t* service, const void* ptr) {
    if (service == NULL || ptr == NULL) {
        return IPC_SHM_HANDLE_INVALID;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;
    /* mem_pool 为 ipc_shm_pool_t 数组成员；需 ipc_service.h 完整类型（本文件经 ipc_shared_mem.h 已包含） */
    const uint8_t* pool_start = pool->mem_pool;
    const uint8_t* pool_end = pool_start + sizeof(pool->mem_pool);
    const uint8_t* p = (const uint8_t*) ptr;

    if (p < pool_start || p >= pool_end) {
        return IPC_SHM_HANDLE_INVALID;
    }

    const size_t block_stride = CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE;
    const size_t offset = (size_t) (p - pool_start);

    const uint32_t index = (uint32_t) (offset / block_stride);

    if (index >= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE) {
        return IPC_SHM_HANDLE_INVALID;
    }

    ipc_shm_block_t* block = &pool->blocks[index];

    /* 验证 ptr 落在此块的有效物理范围内（允许块内任意偏移） */
    const uint8_t* block_start = (const uint8_t*) block->ptr;
    const uint8_t* block_end = block_start + block_stride;
    if (p < block_start || p >= block_end) {
        return IPC_SHM_HANDLE_INVALID;
    }

    ipc_shm_handle_t handle = IPC_SHM_HANDLE_INVALID;

    k_mutex_lock(&block->lock, K_FOREVER);
    if (block->state != IPC_SHM_STATE_FREE && block->state != IPC_SHM_STATE_INVALID) {
        handle = encode_handle(pool, index);
    }
    k_mutex_unlock(&block->lock);

    return handle;
}

/**
 * @brief 获取共享内存池统计信息
 */
void ipc_shm_get_stats(ipc_service_t* service, uint32_t* out_active_count, uint32_t* out_peak_count,
                       uint32_t* out_free_count) {
    /* SIL-2: 验证 service */
    if (service == NULL) {
        return;
    }

    ipc_shm_pool_t* pool = &service->shm_pool;

    k_mutex_lock(&pool->pool_lock, K_FOREVER);

    if (out_active_count != NULL) {
        *out_active_count = (uint32_t) atomic_get(&pool->active_count);
    }

    if (out_peak_count != NULL) {
        *out_peak_count = pool->peak_count;
    }

    if (out_free_count != NULL) {
        *out_free_count = CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE - (uint32_t) atomic_get(&pool->active_count);
    }

    k_mutex_unlock(&pool->pool_lock);
}

#endif /* CONFIG_THREAD_IPC_SERVICE_SHARED_MEM */
