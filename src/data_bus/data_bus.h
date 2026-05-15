/**
 * @file data_bus.h
 * @brief Data Bus 公共 API - 命名通道、引用计数流数据共享
 *
 * 独立于事件系统（可选桥接）。
 * 支持 ISR 和线程上下文统一发布。
 *
 * 核心设计：统一零拷贝 + 自动释放 + 显式 Retain（ARC 风格）
 * - 默认自动释放：回调返回后框架自动 release
 * - 异步持有：回调内调用 data_bus_block_retain()，稍后手动 release
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：删除 COPY 模式，统一 auto_release + retain
 *
 */

#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

typedef struct data_bus_block data_bus_block_t;
typedef struct data_bus_channel data_bus_channel_t;
typedef struct data_bus_consumer data_bus_consumer_t;

/* ============================================================================
 * Callback types
 * ============================================================================ */

/**
 * @brief Data bus consumer callback
 *
 * @param ch        Channel that produced this block
 * @param block     Data block (shared zero-copy reference)
 * @param user_data User data from registration
 *
 * @note The framework automatically calls data_bus_block_release() after the
 *       callback returns. If you need to hold the block beyond the callback
 *       (e.g. pass to another thread), call data_bus_block_retain() inside
 *       the callback and release it later when done.
 */
typedef void (*data_bus_consume_fn_t)(data_bus_channel_t* ch,
                                       data_bus_block_t* block,
                                       void* user_data);

/* ============================================================================
 * Consumer configuration
 * ============================================================================ */

typedef struct {
    const char*             name;           /**< Consumer name (for debugging); copied on register */
    bool                    manual_release; /**< Default false (auto-release enabled).
                                                  Set true if you will call data_bus_block_release()
                                                  yourself inside the callback. */
    data_bus_consume_fn_t   callback;       /**< Data arrival callback */
    void*                   user_data;      /**< Callback user data */
} data_bus_consumer_cfg_t;

/* ============================================================================
 * Data block
 * ============================================================================ */

struct data_bus_block {
    void*           ptr;        /**< Data pointer (from slab or k_malloc) */
    size_t          len;        /**< Data length */
    atomic_t        ref_count;  /**< Reference count */
    struct k_mem_slab* slab;    /**< Source slab (NULL = k_malloc) */
    uint32_t        seq;        /**< Monotonically increasing sequence number */
};

/* ============================================================================
 * Consumer
 * ============================================================================ */

struct data_bus_consumer {
    const char*             name;
    char                    name_storage[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
    bool                    manual_release;
    data_bus_consume_fn_t   callback;
    void*                   user_data;
    uint32_t                last_seq;       /**< Last consumed sequence number */
    bool                    active;         /**< Distributor and unregister may race;
                                                 naturally atomic on 32-bit architectures */
};

/* ============================================================================
 * Channel
 * ============================================================================ */

struct data_bus_channel {
    const char*     name;
    char            name_storage[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
    struct ring_buf queue;              /**< Ring buffer storing data_bus_block_t* */
    uint8_t         queue_buf[CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH * sizeof(void*)];
    struct k_spinlock lock;
    bool            active;

    data_bus_consumer_t consumers[CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL];
    uint32_t        consumer_count;

    uint32_t        next_seq;           /**< Next sequence number (wraps at 2^32) */
    uint32_t        publish_count;
    uint32_t        drop_count;
    uint32_t        queue_full_count;
    uint32_t        alloc_fail_count;
    uint32_t        peak_queue_usage;   /**< Historical max used slots */
    uint32_t        queue_used;         /**< Current used slots (protected by lock) */
    atomic_t        dispatch_hold;      /**< Dispatcher pins channel while dequeuing/dispatching */
};

/* ============================================================================
 * Statistics
 * ============================================================================ */

typedef struct {
    uint32_t publish_count;      /**< Publish count */
    uint32_t drop_count;         /**< Drop count (ring_buf full) */
    uint32_t queue_full_count;   /**< Queue full count */
    uint32_t alloc_fail_count;   /**< Memory allocation failure count */
    uint32_t consumer_count;     /**< Current consumer count */
    uint32_t peak_queue_usage;   /**< Historical max queue usage (slots) */
} data_bus_stats_t;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/**
 * @brief Initialize the data bus
 *
 * Initializes global semaphore, channel table, ring_buf meta-state,
 * and creates/starts the dispatcher thread.
 *
 * @return 0 on success, negative errno on failure
 */
int data_bus_init(void);

/**
 * @brief Deinitialize the data bus
 *
 * Stops accepting new publishes, drains all channel queues,
 * releases all pending blocks, destroys all channels.
 *
 * @warning Cannot reclaim blocks still held by application threads via retain().
 *          Callers must ensure all async consumers have released.
 *
 * @return 0 on success
 */
int data_bus_deinit(void);

/* ============================================================================
 * Channel management
 * ============================================================================ */

/**
 * @brief Create a named channel
 *
 * @param name        Channel name (globally unique, NUL-terminated); copied into the channel
 * @param out_channel Output: channel object pointer
 * @return 0 on success, -EEXIST if name already exists, -EINVAL if name invalid,
 *         -ENOMEM if channel pool exhausted
 *
 * @note Queue depth determined by CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH.
 *       queue_buf is an embedded fixed-size array, no dynamic allocation needed.
 * @note Channel object is obtained from an internal pre-allocated pool
 *       (K_MEM_SLAB or static array), does not depend on k_malloc.
 */
int data_bus_channel_create(const char* name,
                            data_bus_channel_t** out_channel);

/**
 * @brief Destroy a channel
 *
 * Returns -EBUSY if active consumers remain, -EAGAIN if queue not empty or the
 * dispatcher is still delivering a block for this channel (retry later).
 * Caller must unregister all consumers and wait for queue to drain.
 *
 * @return 0 on success, negative errno on failure
 */
int data_bus_channel_destroy(data_bus_channel_t* ch);

/**
 * @brief Find a channel by name
 * @return Channel pointer, or NULL if not found
 */
data_bus_channel_t* data_bus_channel_find(const char* name);

/* ============================================================================
 * Publishing (ISR / thread unified interface)
 * ============================================================================ */

/**
 * @brief Publish data to a channel (unified ISR / thread interface)
 *
 * Automatically detects context and adapts internally:
 * - ISR: allocates from slab only (K_NO_WAIT), k_spin_lock protects ring_buf
 * - Thread: allocates from slab (or k_malloc fallback), k_spin_lock protects
 *
 * After enqueuing, signals the dispatcher thread via semaphore.
 * The dispatcher thread delivers data to all registered consumers.
 *
 * @param ch    Target channel
 * @param data  Data pointer
 * @param len   Data length (bytes)
 * @return 0 on success, negative errno on failure
 *
 * @note In ISR path, slab exhaustion returns -ENOMEM (no k_malloc fallback)
 * @note Data is copied into an internally managed block
 */
int data_bus_publish(data_bus_channel_t* ch, const void* data, size_t len);

/**
 * @brief Publish a pre-allocated block (zero-copy)
 *
 * Caller transfers block ownership to the data bus.
 * Block must be allocated via data_bus_mem_alloc() or compatible slab allocation.
 *
 * Caller is responsible for: ptr, len, slab (data already filled, slab recorded)
 * publish_block is responsible for: seq (from channel next_seq),
 *                                   ref_count = 1 on successful enqueue
 *
 * @pre  Block not yet in any channel queue; typically ref_count == 0
 * @post On success ref_count == 1 (bus holds the reference)
 * @note The bus takes ownership; do not release after publish_block succeeds
 */
int data_bus_publish_block(data_bus_channel_t* ch, data_bus_block_t* block);

/* ============================================================================
 * Consumer management
 * ============================================================================ */

/**
 * @brief Register a consumer on a channel
 *
 * @param ch            Target channel
 * @param cfg           Consumer configuration
 * @param out_consumer  Output: consumer object pointer (optional, may be NULL)
 * @return 0 on success, -EINVAL if cfg invalid, -ENOMEM if consumer table full
 */
int data_bus_consumer_register(data_bus_channel_t* ch,
                                const data_bus_consumer_cfg_t* cfg,
                                data_bus_consumer_t** out_consumer);

/**
 * @brief Unregister a consumer
 *
 * Immediately removed from channel consumer list. If consumer is currently
 * processing data in a callback (and has retained the block),
 * subsequent release is unaffected because reference counting is on the block.
 */
int data_bus_consumer_unregister(data_bus_consumer_t* consumer);

/* ============================================================================
 * Memory management (reference counting)
 * ============================================================================ */

/** @brief Increment reference count */
void data_bus_block_acquire(data_bus_block_t* block);

/** @brief Decrement reference count, free when zero */
void data_bus_block_release(data_bus_block_t* block);

/**
 * @brief Retain a block for asynchronous use (beyond callback scope)
 *
 * Call inside a consumer callback to take an extra reference.
 * You must later call data_bus_block_release() when done.
 *
 * @return The retained block (same pointer, ref_count incremented)
 */
data_bus_block_t* data_bus_block_retain(data_bus_block_t* block);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Get channel statistics
 * @note Best-effort consistency; does not guarantee snapshot matches single block
 */
void data_bus_channel_get_stats(const data_bus_channel_t* ch,
                                 data_bus_stats_t* stats);

/** @brief Reset channel statistics */
void data_bus_reset_stats(data_bus_channel_t* ch);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_H */
