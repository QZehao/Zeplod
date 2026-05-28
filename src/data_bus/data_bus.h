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
 * @version 2.1
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：删除 COPY 模式，统一 auto_release + retain
 * 2026-05-20       2.1            zeh            增加 data_bus_block_ptr / data_bus_block_len
 *
 */

#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 前置声明（完整定义见 data_bus_internal.h，应用代码请勿解引用内部字段）
 * ============================================================================ */

typedef struct data_bus_block    data_bus_block_t;
typedef struct data_bus_channel  data_bus_channel_t;
typedef struct data_bus_consumer data_bus_consumer_t;

/* ============================================================================
 * 回调类型
 * ============================================================================ */

/**
 * @brief Data Bus 消费者回调
 *
 * @param ch        产生此数据块的通道
 * @param block     数据块（共享零拷贝引用）
 * @param user_data 注册时的用户数据
 *
 * @note 框架在回调返回后自动调用 data_bus_block_release()。
 *       如需在回调外继续持有数据块（例如传给另一个线程），
 *       在回调内调用 data_bus_block_retain()，用完后自行 release。
 * @note manual_release=true 时：框架不在回调返回后自动 release；须在返回前自行
 *       data_bus_block_release()。若还调用了 retain()，须 release 两次（隐式引用 +
 *       显式 retain 各一次）。
 * @note 回调中勿调用 data_bus_channel_create/destroy、data_bus_consumer_register/unregister
 *       或 data_bus_deinit()，以免与分发线程争用内部生命周期锁或 k_thread_join 死锁。
 */
typedef void (*data_bus_consume_fn_t)(data_bus_channel_t* ch, data_bus_block_t* block, void* user_data);

/* ============================================================================
 * 消费者配置
 * ============================================================================ */

typedef struct {
    const char* name;                /**< 消费者名称（调试用）；注册时拷贝 */
    bool        manual_release;      /**< 默认 false。true 时回调返回前须 release；
                                           若同时使用 retain() 须 release 两次，见 data_bus_consume_fn_t */
    data_bus_consume_fn_t callback;  /**< 数据到达回调 */
    void*                 user_data; /**< 回调用户数据 */
} data_bus_consumer_cfg_t;

/* ============================================================================
 * 统计
 * ============================================================================ */

typedef struct {
    uint32_t publish_count;    /**< 发布次数 */
    uint32_t drop_count;       /**< 丢弃次数（ring_buf 满） */
    uint32_t queue_full_count; /**< 队列满次数 */
    uint32_t alloc_fail_count; /**< 内存分配失败次数 */
    uint32_t consumer_count;   /**< 当前消费者数量 */
    uint32_t peak_queue_usage; /**< 历史最大队列使用量（槽位） */
} data_bus_stats_t;

/* ============================================================================
 * 生命周期
 * ============================================================================ */

/**
 * @brief 初始化 Data Bus
 *
 * 初始化全局信号量、通道表、ring_buf 元状态，
 * 并创建/启动分发线程。
 *
 * @return 成功返回 0，失败返回负 errno
 */
int data_bus_init(void);

/**
 * @brief 反初始化 Data Bus
 *
 * 停止接收新发布，排空所有通道队列，
 * 释放所有挂起的数据块，销毁所有通道。
 *
 * @warning 切勿在消费者回调或任何由分发线程调用的上下文中调用本函数，
 *          否则 k_thread_join() 将等待分发线程结束而导致死锁。
 * @warning 无法回收应用线程通过 retain() 持有的数据块。
 *          调用者必须确保所有异步消费者已 release。
 *
 * @return 成功返回 0；从分发线程调用返回 -EINVAL；
 *         分发线程 join 超时返回 -EIO（`g_initialized` / `g_shutting_down` 保持为 1，可重试 deinit）
 */
int data_bus_deinit(void);

/* ============================================================================
 * 通道管理
 * ============================================================================ */

/**
 * @brief 创建命名通道
 *
 * @param name        通道名称（全局唯一，NUL 结尾）；拷贝到通道内部
 * @param out_channel 输出：通道对象指针
 * @return 成功返回 0，-EEXIST 名称已存在，-EINVAL 名称非法，
 *         -ENOMEM 通道池耗尽
 *
 * @note 队列深度由 CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH 决定。
 *       queue_buf 是内嵌固定大小数组，无需动态分配。
 * @note 通道对象来自内部预分配池（K_MEM_SLAB 或静态数组），不依赖 k_malloc。
 */
int data_bus_channel_create(const char* name, data_bus_channel_t** out_channel);

/**
 * @brief 销毁通道
 *
 * 若仍有活跃消费者返回 -EBUSY；
 * 若队列非空、发布者仍在入队或分发器仍在为此通道投递数据块返回 -EAGAIN（稍后重试）。
 * 调用者必须先注销所有消费者并等待队列排空。
 * 通过检查后会先将通道置为非 active，再从全局表移除，避免销毁窗口内继续 publish。
 *
 * @return 成功返回 0；-EAGAIN 时队列未空、publish_hold 或 dispatch_hold 非零，宜退避重试
 */
int data_bus_channel_destroy(data_bus_channel_t* ch);

/**
 * @brief 按名称查找通道
 * @return 通道指针，未找到返回 NULL
 *
 * @warning 返回指针仅在通道未被 destroy 期间有效；勿跨线程长期缓存。
 *          其他线程 destroy 同名通道后，已保存的指针将悬空（UAF）。
 */
data_bus_channel_t* data_bus_channel_find(const char* name);

/* ============================================================================
 * 发布（ISR / 线程统一接口）
 * ============================================================================ */

/**
 * @brief 向通道发布数据（ISR / 线程统一接口）
 *
 * 自动检测上下文并内部适配：
 * - ISR：仅从 slab 分配（K_NO_WAIT），k_spin_lock 保护 ring_buf
 * - 线程：从 slab 分配（或 k_malloc 兜底），k_spin_lock 保护
 *
 * 入队后通过信号量通知分发线程。
 * 分发线程将数据投递给所有已注册消费者。
 *
 * @param ch    目标通道
 * @param data  数据指针
 * @param len   数据长度（字节）
 * @return 成功返回 0，失败返回负 errno
 *
 * @note ISR 路径中 slab 耗尽返回 -ENOMEM（无 k_malloc 兜底）
 * @note ISR 发布者必须在 data_bus_channel_destroy()/data_bus_deinit() 前停止，
 *       否则已缓存的通道指针可能在销毁后失效。
 * @note 数据被拷贝到内部管理的块中
 * @note 无已注册消费者时仍会入队；分发线程会排空队列并 release 块，避免块滞留泄漏
 * @note len 必须大于 0；不支持零长度载荷（无心跳语义）
 */
int data_bus_publish(data_bus_channel_t* ch, const void* data, size_t len);

/**
 * @brief 发布预分配的数据块（零拷贝）
 *
 * 调用者将块所有权转移给 Data Bus。
 * 块必须通过 data_bus_mem_alloc() 或兼容的 slab 分配。
 *
 * 调用者负责：ptr、len、slab（数据已填充，slab 已记录）
 * publish_block 负责：seq（来自通道 next_seq），
 *                      成功入队时 ref_count = 1
 *
 * @pre  块尚未进入任何通道队列；ref_count 必须为 0，否则返回 -EINVAL
 * @post 成功时 ref_count == 1（bus 持有引用）
 * @note bus 接管所有权；publish_block 成功后不要 release
 * @note 入队失败（如 -ENOBUFS）时块仍归调用方且 ref_count 为 0，须 data_bus_mem_free()
 * @note 无消费者时与 data_bus_publish 相同：分发线程排空并 release
 */
int data_bus_publish_block(data_bus_channel_t* ch, data_bus_block_t* block);

/* ============================================================================
 * 消费者管理
 * ============================================================================ */

/**
 * @brief 在通道上注册消费者
 *
 * @param ch            目标通道
 * @param cfg           消费者配置
 * @param out_consumer  输出：消费者对象指针（可选，可为 NULL）；槽位地址固定，注销后勿再使用
 * @return 成功返回 0，-EINVAL 配置非法，-ENOMEM 消费者表满
 */
int data_bus_consumer_register(data_bus_channel_t* ch, const data_bus_consumer_cfg_t* cfg,
                               data_bus_consumer_t** out_consumer);

/**
 * @brief 注销消费者
 *
 * 将槽位标记为空闲，不移动其他消费者对象，out_consumer 指针在注销后失效。
 * 若消费者当前正在回调中处理数据（且已 retain 了数据块），后续 release 不受影响。
 */
int data_bus_consumer_unregister(data_bus_consumer_t* consumer);

/* ============================================================================
 * 内存管理（引用计数）
 * ============================================================================ */

/** @brief 增加引用计数 */
void data_bus_block_acquire(data_bus_block_t* block);

/**
 * @brief 减少引用计数，归零时释放
 *
 * @note 每个 retain/acquire 必须严格对应一次 release；重复 release 在 DEBUG 下触发断言
 */
void data_bus_block_release(data_bus_block_t* block);

/**
 * @brief 保留数据块供异步使用（超出回调作用域）
 *
 * 在消费者回调内调用以获取额外引用。
 * 稍后必须调用 data_bus_block_release()。
 *
 * @return 被保留的块（同一指针，ref_count 已增加）
 */
data_bus_block_t* data_bus_block_retain(data_bus_block_t* block);

/**
 * @brief 只读访问数据块载荷指针
 *
 * @param block 数据块；NULL 时返回 NULL
 * @return 载荷指针；在块仍被有效持有（未 release 至销毁）期间有效
 */
void* data_bus_block_ptr(const data_bus_block_t* block);

/**
 * @brief 只读访问数据块载荷长度（字节）
 *
 * @param block 数据块；NULL 时返回 0
 */
size_t data_bus_block_len(const data_bus_block_t* block);

/* ============================================================================
 * 统计
 * ============================================================================ */

/**
 * @brief 获取通道统计
 * @note 尽力保证一致性；不保证快照与单个块匹配
 */
void data_bus_channel_get_stats(const data_bus_channel_t* ch, data_bus_stats_t* stats);

/** @brief 重置通道统计 */
void data_bus_reset_stats(data_bus_channel_t* ch);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_H */
