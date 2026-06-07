/**
 * @file data_bus_channel.c
 * @brief Data Bus 通道管理 - 创建/销毁/查找/发布
 *
 * 通道对象从预分配 slab 池中获取，不依赖 k_malloc。
 * 发布时将数据拷贝到内部管理的 block，然后通过信号量通知分发线程。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：适配统一 auto_release 模型
 * 2026-05-19       2.1            zeh            destroy 移表前先 active=0，堵住悬空 publish
 * 2026-05-20       2.2            zeh            obj_init 运行时校验；消费者固定槽位 reset/destroy
 * 2026-05-20       2.3            zeh            destroy 在 ch->lock 下置 inactive 并排空；publish 入队前二次校验
 * active 2026-05-20       2.4            zeh            enqueue 持锁二次校验 active；入队失败统一回滚；deinit 去重
 * drain
 *
 */

#include "data_bus_channel.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include "data_bus_consumer.h"
#include "data_bus_internal.h"
#include "data_bus_memory.h"
#include "data_bus_ready.h"

LOG_MODULE_REGISTER(data_bus_channel, CONFIG_DATA_BUS_LOG_LEVEL);

/* ============================================================================
 * 通道对象 slab
 * ============================================================================ */

K_MEM_SLAB_DEFINE(data_bus_channel_slab, sizeof(data_bus_channel_t), CONFIG_DATA_BUS_MAX_CHANNELS, sizeof(void*));

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

static bool name_valid(const char* name) {
    if (name == NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len == 0 || len >= CONFIG_DATA_BUS_CHANNEL_NAME_MAX) {
        return false;
    }
    return true;
}

static data_bus_channel_t* find_in_table(const char* name) {
    for (uint32_t i = 0; i < g_channel_count; i++) {
        data_bus_channel_t* ch = g_channels[i];
        if (ch != NULL && atomic_get(&ch->active) && strcmp(ch->name, name) == 0) {
            return ch;
        }
    }
    return NULL;
}

static bool channel_in_table_locked(const data_bus_channel_t* ch) {
    for (uint32_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i] == ch) {
            return true;
        }
    }
    return false;
}

static bool channel_note_block_memory(data_bus_channel_t* ch, data_bus_block_t* block) {
    if (ch == NULL || block == NULL || (!block->malloc_fallback && !block->slab_exhausted)) {
        return false;
    }

    bool             should_warn = false;
    k_spinlock_key_t key = k_spin_lock(&ch->lock);

    if (block->memory_stats_accounted) {
        k_spin_unlock(&ch->lock, key);
        return false;
    }

    if (block->malloc_fallback) {
        ch->malloc_fallback_count++;
#if CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD > 0
        should_warn = (ch->malloc_fallback_count == CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD);
#endif
    }
    if (block->slab_exhausted) {
        ch->slab_exhausted_count++;
    }
    block->memory_stats_accounted = true;

    k_spin_unlock(&ch->lock, key);
    return should_warn;
}

static int channel_publish_acquire(data_bus_channel_t* ch, bool in_isr) {
    if (ch == NULL) {
        return -EINVAL;
    }

    if (atomic_get(&g_shutting_down)) {
        return -ESHUTDOWN;
    }

    if (in_isr) {
        k_spinlock_key_t key = k_spin_lock(&ch->lock);
        if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
            k_spin_unlock(&ch->lock, key);
            return -ESHUTDOWN;
        }
        (void) atomic_inc(&ch->publish_hold);
        k_spin_unlock(&ch->lock, key);
        return 0;
    }

    k_mutex_lock(&g_channels_lock, K_FOREVER);
    if (atomic_get(&g_shutting_down)) {
        k_mutex_unlock(&g_channels_lock);
        return -ESHUTDOWN;
    }
    if (!channel_in_table_locked(ch)) {
        k_mutex_unlock(&g_channels_lock);
        return -EINVAL;
    }

    k_spinlock_key_t key = k_spin_lock(&ch->lock);
    if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
        k_spin_unlock(&ch->lock, key);
        k_mutex_unlock(&g_channels_lock);
        return -ESHUTDOWN;
    }
    (void) atomic_inc(&ch->publish_hold);
    k_spin_unlock(&ch->lock, key);
    k_mutex_unlock(&g_channels_lock);
    return 0;
}

static void channel_publish_release(data_bus_channel_t* ch) {
    if (ch != NULL) {
        (void) atomic_dec(&ch->publish_hold);
    }
}

static bool channel_is_overwrite(const data_bus_channel_t* ch) {
    return (ch->flags & DATA_BUS_CHANNEL_OVERWRITE) != 0U;
}

static uint32_t channel_queue_capacity(const data_bus_channel_t* ch) {
    return channel_is_overwrite(ch) ? 1U : CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH;
}

static data_bus_block_t* channel_queue_peek_front(data_bus_channel_t* ch) {
    if (ch->queue_used == 0U) {
        return NULL;
    }
    return ch->queue[ch->queue_head];
}

static data_bus_block_t* channel_queue_dequeue(data_bus_channel_t* ch) {
    if (ch->queue_used == 0U) {
        return NULL;
    }

    data_bus_block_t* block = ch->queue[ch->queue_head];

    ch->queue_head = (ch->queue_head + 1U) % CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH;
    ch->queue_used--;
    return block;
}

static bool channel_queue_enqueue(data_bus_channel_t* ch, data_bus_block_t* block) {
    if (ch->queue_used >= channel_queue_capacity(ch)) {
        return false;
    }

    ch->queue[ch->queue_tail] = block;
    ch->queue_tail = (ch->queue_tail + 1U) % CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH;
    ch->queue_used++;
    return true;
}

/* ============================================================================
 * 通道对象初始化/重置
 * ============================================================================ */

int data_bus_channel_obj_init(data_bus_channel_t* ch, const char* name, uint32_t flags) {
    if (ch == NULL || name == NULL) {
        return -EINVAL;
    }

    memset(ch, 0, sizeof(*ch));

    {
        int name_ret = snprintf(ch->name_storage, sizeof(ch->name_storage), "%s", name);

        if (name_ret < 0 || (size_t) name_ret >= sizeof(ch->name_storage)) {
            return -EINVAL;
        }
    }

    ch->name = ch->name_storage;
    ch->flags = flags;
    ch->queue_head = 0U;
    ch->queue_tail = 0U;
    ch->queue_used = 0U;
    /* k_spinlock 零初始化是有效的 */
    atomic_set(&ch->active, 1);
    ch->next_seq = 0;
    atomic_set(&ch->publish_hold, 0);
    atomic_set(&ch->dispatch_hold, 0);
    atomic_set(&ch->lifecycle_hold, 0);
    atomic_set(&ch->dispatch_ready, 0);

    return 0;
}

void data_bus_channel_drain_pending(data_bus_channel_t* ch, bool run_dispatch) {
    if (ch == NULL) {
        return;
    }

    while (true) {
        data_bus_block_t* block = NULL;

        k_spinlock_key_t key = k_spin_lock(&ch->lock);
        if (ch->queue_used > 0U) {
            block = channel_queue_dequeue(ch);
        }
        k_spin_unlock(&ch->lock, key);

        if (block == NULL) {
            break;
        }

        if (run_dispatch) {
            LOG_DBG("dispatch ch='%s' seq=%u len=%zu", ch->name, block->seq, block->len);
            data_bus_consumer_dispatch(ch, block);
        }

        data_bus_block_release(block);
    }
}

void data_bus_channel_obj_reset(data_bus_channel_t* ch) {
    if (ch == NULL) {
        return;
    }

    data_bus_channel_drain_pending(ch, false);

    /* 清空所有消费者槽位 */
    for (uint32_t i = 0; i < CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL; i++) {
        if (ch->consumer_slot_in_use[i]) {
            atomic_set(&ch->consumers[i].active, 0);
            ch->consumers[i].channel = NULL;
        }
        ch->consumer_slot_in_use[i] = false;
        memset(&ch->consumers[i], 0, sizeof(ch->consumers[i]));
    }
    ch->consumer_count = 0;

    atomic_set(&ch->active, 0);
    atomic_set(&ch->dispatch_ready, 0);
}
/* ============================================================================
 * 公共 API: 通道管理
 * ============================================================================ */

int data_bus_channel_create(const char* name, data_bus_channel_t** out_channel) {
    static const data_bus_channel_cfg_t default_cfg = {.flags = DATA_BUS_CHANNEL_DEFAULT};

    return data_bus_channel_create_ex(name, &default_cfg, out_channel);
}

int data_bus_channel_create_ex(const char* name, const data_bus_channel_cfg_t* cfg,
                               data_bus_channel_t** out_channel) {
    int ready = data_bus_require_initialized();

    if (ready != 0) {
        return ready;
    }
    if (out_channel == NULL || cfg == NULL) {
        return -EINVAL;
    }
    if (!name_valid(name)) {
        return -EINVAL;
    }

    k_mutex_lock(&g_channels_lock, K_FOREVER);
    if (!atomic_get(&g_initialized)) {
        k_mutex_unlock(&g_channels_lock);
        return -ENODEV;
    }
    if (atomic_get(&g_shutting_down)) {
        k_mutex_unlock(&g_channels_lock);
        return -ESHUTDOWN;
    }
    /* 检查重复名称 */
    if (find_in_table(name) != NULL) {
        LOG_WRN("Channel '%s' already exists", name);
        k_mutex_unlock(&g_channels_lock);
        return -EEXIST;
    }

    /* 在表中找一个空槽 */
    if (g_channel_count >= CONFIG_DATA_BUS_MAX_CHANNELS) {
        k_mutex_unlock(&g_channels_lock);
        return -ENOMEM;
    }

    /* 从 slab 分配通道对象 */
    void*               mem = NULL;
    int                 ret = k_mem_slab_alloc(&data_bus_channel_slab, &mem, K_NO_WAIT);
    data_bus_channel_t* ch = (data_bus_channel_t*) mem;
    if (ret != 0) {
        LOG_ERR("Channel slab exhausted (max=%u)", CONFIG_DATA_BUS_MAX_CHANNELS);
        k_mutex_unlock(&g_channels_lock);
        return -ENOMEM;
    }

    /* 初始化通道 */
    ret = data_bus_channel_obj_init(ch, name, cfg->flags);
    if (ret != 0) {
        k_mem_slab_free(&data_bus_channel_slab, ch);
        k_mutex_unlock(&g_channels_lock);
        return ret;
    }

    /* 添加到全局表 */
    g_channels[g_channel_count++] = ch;
    uint32_t total = g_channel_count;
    *out_channel = ch;

    k_mutex_unlock(&g_channels_lock);

    LOG_DBG("Channel '%s' created (total=%u/%u flags=0x%x)", name, total, CONFIG_DATA_BUS_MAX_CHANNELS, cfg->flags);
    return 0;
}

int data_bus_channel_destroy(data_bus_channel_t* ch) {
    int ready = data_bus_require_initialized();

    if (ready != 0) {
        return ready;
    }
    if (ch == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&g_channels_lock, K_FOREVER);
    if (!channel_in_table_locked(ch)) {
        k_mutex_unlock(&g_channels_lock);
        return -EINVAL;
    }
    if (atomic_get(&g_shutting_down)) {
        k_mutex_unlock(&g_channels_lock);
        return -ESHUTDOWN;
    }
    if (!atomic_get(&ch->active)) {
        k_mutex_unlock(&g_channels_lock);
        return -EINVAL;
    }

    /* 在 ch->lock 下原子地检查消费者/队列并关闭发布，避免与 register 竞态 */
    k_spinlock_key_t key = k_spin_lock(&ch->lock);
    if (ch->consumer_count > 0) {
        LOG_WRN("Channel '%s' destroy failed: consumers remain (count=%u)", ch->name, ch->consumer_count);
        k_spin_unlock(&ch->lock, key);
        k_mutex_unlock(&g_channels_lock);
        return -EBUSY;
    }
    if (ch->queue_used > 0) {
        k_spin_unlock(&ch->lock, key);
        k_mutex_unlock(&g_channels_lock);
        return -EAGAIN;
    }
    atomic_set(&ch->active, 0);
    k_spin_unlock(&ch->lock, key);

    if (atomic_get(&ch->publish_hold) != 0 || atomic_get(&ch->dispatch_hold) != 0 ||
        atomic_get(&ch->lifecycle_hold) != 0) {
        atomic_set(&ch->active, 1);
        k_mutex_unlock(&g_channels_lock);
        return -EAGAIN;
    }

    /* 排空在「队列已空」检查之后、移表之前可能入队的残留块（publish 二次 active 校验前窗口） */
    data_bus_channel_drain_pending(ch, false);

    key = k_spin_lock(&ch->lock);
    bool queue_empty = (ch->queue_used == 0);
    k_spin_unlock(&ch->lock, key);

    if (!queue_empty) {
        atomic_set(&ch->active, 1);
        data_bus_ready_resync(ch);
        k_mutex_unlock(&g_channels_lock);
        return -EAGAIN;
    }

    if (atomic_get(&ch->publish_hold) != 0 || atomic_get(&ch->dispatch_hold) != 0 ||
        atomic_get(&ch->lifecycle_hold) != 0) {
        atomic_set(&ch->active, 1);
        data_bus_ready_resync(ch);
        k_mutex_unlock(&g_channels_lock);
        return -EAGAIN;
    }

    key = k_spin_lock(&ch->lock);
    atomic_set(&ch->dispatch_ready, 0);
    k_spin_unlock(&ch->lock, key);

    /* 从表中移除 */
    for (uint32_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i] == ch) {
            /* 通过移位压缩表 */
            for (uint32_t j = i; j < g_channel_count - 1; j++) {
                g_channels[j] = g_channels[j + 1];
            }
            g_channels[--g_channel_count] = NULL;
            break;
        }
    }

    LOG_DBG("Channel '%s' destroyed", ch->name);

    k_mem_slab_free(&data_bus_channel_slab, ch);

    k_mutex_unlock(&g_channels_lock);
    return 0;
}

data_bus_channel_t* data_bus_channel_find(const char* name) {
    if (name == NULL) {
        return NULL;
    }
    if (data_bus_require_initialized() != 0) {
        return NULL;
    }

    k_mutex_lock(&g_channels_lock, K_FOREVER);
    data_bus_channel_t* ch = find_in_table(name);
    k_mutex_unlock(&g_channels_lock);

    return ch;
}

/* ============================================================================
 * 内部辅助：将块入队到通道
 * ============================================================================ */

static bool channel_enqueue_block(data_bus_channel_t* ch, data_bus_block_t* block, uint32_t* out_seq) {
#if IS_ENABLED(CONFIG_DATA_BUS_DEBUG_REFCNT)
    __ASSERT(atomic_get(&block->ref_count) == 0, "block ref_count must be 0 before enqueue");
#endif

    k_spinlock_key_t key = k_spin_lock(&ch->lock);

    /* SMP：与 destroy 竞态时拒绝入队，避免访问已释放的 ch（调用方须 mem_free block） */
    if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
        k_spin_unlock(&ch->lock, key);
        return false;
    }

    if (channel_is_overwrite(ch) && ch->queue_used > 0U) {
        data_bus_block_t* old = channel_queue_peek_front(ch);

        if (old != NULL && atomic_get(&old->ref_count) == 1) {
            (void) channel_queue_dequeue(ch);
            data_bus_block_release(old);
            ch->drop_count++;
        } else {
            /* 旧块仍被 retain：丢弃新数据，避免 UAF */
            k_spin_unlock(&ch->lock, key);
            return false;
        }
    } else if (ch->queue_used >= channel_queue_capacity(ch)) {
        k_spin_unlock(&ch->lock, key);
        return false;
    }

    uint32_t seq = ch->next_seq;
    block->seq = seq;
    atomic_set(&block->ref_count, 1);

    if (!channel_queue_enqueue(ch, block)) {
        atomic_set(&block->ref_count, 0);
        k_spin_unlock(&ch->lock, key);
        return false;
    }

    ch->next_seq++;
    ch->publish_count++;
    if (out_seq != NULL) {
        *out_seq = seq;
    }
    if (ch->queue_used > ch->peak_queue_usage) {
        ch->peak_queue_usage = ch->queue_used;
    }

    k_spin_unlock(&ch->lock, key);
    return true;
}

/**
 * @brief 入队失败时释放块并更新统计
 * @return -ESHUTDOWN 通道已关闭；-ENOBUFS 队列已满
 */
static int publish_handle_enqueue_failure(data_bus_channel_t* ch, data_bus_block_t* block, bool free_block) {
    if (free_block) {
        data_bus_mem_free(block);
    }

    k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
    if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
        k_spin_unlock(&ch->lock, fkey);
        return -ESHUTDOWN;
    }
    ch->drop_count++;
    if (channel_is_overwrite(ch) && ch->queue_used > 0U) {
        /* overwrite + retain 竞态：旧块仍被持有，新数据被丢弃 */
    } else {
        ch->queue_full_count++;
    }
    k_spin_unlock(&ch->lock, fkey);

    if (channel_is_overwrite(ch)) {
        LOG_WRN("Publish to '%s' dropped: overwrite slot busy (retained block pending)", ch->name);
    } else {
        LOG_WRN("Publish to '%s' dropped: queue full (depth=%u)", ch->name, CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH);
    }
    return -ENOBUFS;
}

/* ============================================================================
 * 公共 API: 发布
 * ============================================================================ */

int data_bus_publish(data_bus_channel_t* ch, const void* data, size_t len) {
    int  ready = data_bus_require_initialized();
    bool in_isr = k_is_in_isr();

    if (ready != 0) {
        return ready;
    }
    if (ch == NULL || (data == NULL && len > 0)) {
        return -EINVAL;
    }
    if (len == 0) {
        return -EINVAL;
    }

    ready = channel_publish_acquire(ch, in_isr);
    if (ready != 0) {
        return ready;
    }
    data_bus_block_t* block = NULL;

    /* 分配块 */
    if (in_isr) {
        block = data_bus_mem_alloc_isr(len);
    } else {
        block = data_bus_mem_alloc(len);
    }
    if (block == NULL) {
        LOG_ERR("Publish to '%s' failed: block allocation failed (len=%zu)", ch->name, len);
        k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
        ch->alloc_fail_count++;
        k_spin_unlock(&ch->lock, fkey);
        channel_publish_release(ch);
        return -ENOMEM;
    }

    bool warn_memory_fallback = channel_note_block_memory(ch, block);

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE) && (CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD > 0)
    if (warn_memory_fallback && !in_isr) {
        data_bus_stats_t stats;
        data_bus_channel_get_stats(ch, &stats);
        data_bus_event_bridge_notify_memory_warning(ch, stats.malloc_fallback_count, stats.slab_exhausted_count);
    }
#else
    ARG_UNUSED(warn_memory_fallback);
#endif

    /* 拷贝数据 */
    memcpy(block->ptr, data, len);
    /* block->len 已由 mem_alloc 设置，ref_count 已为 0 */

    /* 分配期间 destroy 可能已置 inactive，入队前再次校验 */
    if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
        data_bus_mem_free(block);
        channel_publish_release(ch);
        return -ESHUTDOWN;
    }

    /* 入队 */
    bool     enqueued;
    uint32_t published_seq = 0;
    size_t   published_len = block->len;

    if (in_isr) {
        enqueued = channel_enqueue_block(ch, block, &published_seq);
    } else {
        k_mutex_lock(&g_channels_lock, K_FOREVER);
        if (!channel_in_table_locked(ch)) {
            k_mutex_unlock(&g_channels_lock);
            data_bus_mem_free(block);
            channel_publish_release(ch);
            return -EINVAL;
        }
        if (atomic_get(&g_shutting_down)) {
            k_mutex_unlock(&g_channels_lock);
            data_bus_mem_free(block);
            channel_publish_release(ch);
            return -ESHUTDOWN;
        }
        enqueued = channel_enqueue_block(ch, block, &published_seq);
        k_mutex_unlock(&g_channels_lock);
    }

    if (!enqueued) {
        int err = publish_handle_enqueue_failure(ch, block, true);
        channel_publish_release(ch);
        return err;
    }

    LOG_DBG("Published to '%s' seq=%u len=%zu", ch->name, published_seq, published_len);

    /* 通知分发线程（就绪队列 + 信号量） */
    data_bus_ready_signal(ch);

    /* 事件桥接（仅线程路径） */
#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
    if (!in_isr) {
        data_bus_event_bridge_notify(ch, published_seq, published_len);
    }
#endif

    channel_publish_release(ch);
    return 0;
}

int data_bus_publish_block(data_bus_channel_t* ch, data_bus_block_t* block) {
    int  ready = data_bus_require_initialized();
    bool in_isr = k_is_in_isr();

    if (ready != 0) {
        return ready;
    }
    if (ch == NULL || block == NULL) {
        return -EINVAL;
    }
    if (block->ptr == NULL || block->len == 0) {
        return -EINVAL;
    }
    if (atomic_get(&block->ref_count) != 0) {
        return -EINVAL;
    }

    ready = channel_publish_acquire(ch, in_isr);
    if (ready != 0) {
        return ready;
    }

    bool     enqueued;
    uint32_t published_seq = 0;
    size_t   published_len = block->len;

    bool warn_memory_fallback = channel_note_block_memory(ch, block);

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE) && (CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD > 0)
    if (warn_memory_fallback && !in_isr) {
        data_bus_stats_t stats;
        data_bus_channel_get_stats(ch, &stats);
        data_bus_event_bridge_notify_memory_warning(ch, stats.malloc_fallback_count, stats.slab_exhausted_count);
    }
#else
    ARG_UNUSED(warn_memory_fallback);
#endif

    if (in_isr) {
        enqueued = channel_enqueue_block(ch, block, &published_seq);
    } else {
        k_mutex_lock(&g_channels_lock, K_FOREVER);
        if (!channel_in_table_locked(ch)) {
            k_mutex_unlock(&g_channels_lock);
            channel_publish_release(ch);
            return -EINVAL;
        }
        if (atomic_get(&g_shutting_down)) {
            k_mutex_unlock(&g_channels_lock);
            channel_publish_release(ch);
            return -ESHUTDOWN;
        }
        enqueued = channel_enqueue_block(ch, block, &published_seq);
        k_mutex_unlock(&g_channels_lock);
    }
    if (!enqueued) {
        int err = publish_handle_enqueue_failure(ch, block, false);
        channel_publish_release(ch);
        return err;
    }

    LOG_DBG("publish_block to '%s' seq=%u len=%zu", ch->name, published_seq, published_len);

    /* 通知分发器（就绪队列 + 信号量） */
    data_bus_ready_signal(ch);

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
    if (!in_isr) {
        data_bus_event_bridge_notify(ch, published_seq, published_len);
    }
#endif

    channel_publish_release(ch);
    return 0;
}

int data_bus_publish_inplace(data_bus_channel_t* ch, size_t len, data_bus_fill_fn_t fill, void* user_data) {
    int  ready = data_bus_require_initialized();
    bool in_isr = k_is_in_isr();

    if (ready != 0) {
        return ready;
    }
    if (ch == NULL || fill == NULL) {
        return -EINVAL;
    }
    if (len == 0) {
        return -EINVAL;
    }

    ready = channel_publish_acquire(ch, in_isr);
    if (ready != 0) {
        return ready;
    }

    data_bus_block_t* block = NULL;

    if (in_isr) {
        block = data_bus_mem_alloc_isr(len);
    } else {
        block = data_bus_mem_alloc(len);
    }
    if (block == NULL) {
        LOG_ERR("publish_inplace to '%s' failed: block allocation (len=%zu)", ch->name, len);
        k_spinlock_key_t fkey = k_spin_lock(&ch->lock);
        ch->alloc_fail_count++;
        k_spin_unlock(&ch->lock, fkey);
        channel_publish_release(ch);
        return -ENOMEM;
    }

    bool warn_memory_fallback = channel_note_block_memory(ch, block);

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE) && (CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD > 0)
    if (warn_memory_fallback && !in_isr) {
        data_bus_stats_t stats;
        data_bus_channel_get_stats(ch, &stats);
        data_bus_event_bridge_notify_memory_warning(ch, stats.malloc_fallback_count, stats.slab_exhausted_count);
    }
#else
    ARG_UNUSED(warn_memory_fallback);
#endif

    int fill_ret = fill(block->ptr, len, user_data);
    if (fill_ret != 0) {
        data_bus_mem_free(block);
        channel_publish_release(ch);
        return fill_ret;
    }

    if (!atomic_get(&ch->active) || atomic_get(&g_shutting_down)) {
        data_bus_mem_free(block);
        channel_publish_release(ch);
        return -ESHUTDOWN;
    }

    bool     enqueued;
    uint32_t published_seq = 0;
    size_t   published_len = block->len;

    if (in_isr) {
        enqueued = channel_enqueue_block(ch, block, &published_seq);
    } else {
        k_mutex_lock(&g_channels_lock, K_FOREVER);
        if (!channel_in_table_locked(ch)) {
            k_mutex_unlock(&g_channels_lock);
            data_bus_mem_free(block);
            channel_publish_release(ch);
            return -EINVAL;
        }
        if (atomic_get(&g_shutting_down)) {
            k_mutex_unlock(&g_channels_lock);
            data_bus_mem_free(block);
            channel_publish_release(ch);
            return -ESHUTDOWN;
        }
        enqueued = channel_enqueue_block(ch, block, &published_seq);
        k_mutex_unlock(&g_channels_lock);
    }

    if (!enqueued) {
        int err = publish_handle_enqueue_failure(ch, block, true);
        channel_publish_release(ch);
        return err;
    }

    LOG_DBG("publish_inplace to '%s' seq=%u len=%zu", ch->name, published_seq, published_len);

    data_bus_ready_signal(ch);

#if IS_ENABLED(CONFIG_DATA_BUS_EVENT_BRIDGE)
    if (!in_isr) {
        data_bus_event_bridge_notify(ch, published_seq, published_len);
    }
#endif

    channel_publish_release(ch);
    return 0;
}
