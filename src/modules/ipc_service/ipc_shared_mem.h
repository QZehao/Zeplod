/**
 * @file ipc_shared_mem.h
 * @brief IPC 共享内存池与引用计数管理器
 *
 * 设计目标：
 * - 解决调用者与服务函数之间的数据所有权不明确问题
 * - 提供引用计数自动管理，确保缓冲区在请求处理期间有效
 * - 提供安全的跨线程数据共享机制
 *
 * SIL-2 合规：
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
 * 2026-05-09     1.1            zeh            未启用时 stub 句柄与 active_count 字段说明
 *
 */

#ifndef ZEPHYR_IPC_SHARED_MEM_H_
#define ZEPHYR_IPC_SHARED_MEM_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <stdbool.h>
#include <stdint.h>

#if !IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
/* 当未启用共享内存时提供空实现（编译占位；调用方应在 Kconfig 关闭 SHARED_MEM 时不走分配路径） */
typedef uint32_t ipc_shm_handle_t;
struct ipc_service;

#define IPC_SHM_HANDLE_INVALID ((ipc_shm_handle_t) 0xFFFFFFFFU)

static inline ipc_shm_handle_t ipc_shm_alloc(struct ipc_service* service, size_t size) {
    (void) service;
    (void) size;
    return IPC_SHM_HANDLE_INVALID;
}
static inline void ipc_shm_acquire(struct ipc_service* service, ipc_shm_handle_t handle) {
    (void) service;
    (void) handle;
}
static inline int ipc_shm_release(struct ipc_service* service, ipc_shm_handle_t handle) {
    (void) service;
    (void) handle;
    return 0;
}
static inline void* ipc_shm_get_ptr(struct ipc_service* service, ipc_shm_handle_t handle) {
    (void) service;
    (void) handle;
    return NULL;
}
static inline size_t ipc_shm_get_size(struct ipc_service* service, ipc_shm_handle_t handle) {
    (void) service;
    (void) handle;
    return 0;
}
static inline int ipc_shm_init(struct ipc_service* service) {
    (void) service;
    return 0;
}
static inline void ipc_shm_deinit(struct ipc_service* service) {
    (void) service;
}
static inline bool ipc_shm_is_valid(struct ipc_service* service, ipc_shm_handle_t handle) {
    (void) service;
    (void) handle;
    return false;
}
static inline ipc_shm_handle_t ipc_shm_lookup_handle_by_ptr(struct ipc_service* service, const void* ptr) {
    (void) service;
    (void) ptr;
    return IPC_SHM_HANDLE_INVALID;
}
static inline void ipc_shm_get_stats(struct ipc_service* service, uint32_t* a, uint32_t* p, uint32_t* f) {
    (void) service;
    if (a)
        *a = 0;
    if (p)
        *p = 0;
    if (f)
        *f = 0;
}
#else

#if !IS_ENABLED(CONFIG_THREAD_IPC_SERVICE)
#error "ipc_shared_mem.h requires CONFIG_THREAD_IPC_SERVICE=y"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct ipc_service;

/**
 * @brief 共享内存块状态
 */
typedef enum {
    IPC_SHM_STATE_FREE = 0,      /**< 空闲，可分配 */
    IPC_SHM_STATE_ALLOCATED = 1, /**< 已分配，引用计数=1（分配者持有） */
    IPC_SHM_STATE_SHARED = 2,    /**< 共享中，引用计数>1 */
    IPC_SHM_STATE_INVALID = 3    /**< 无效状态（错误检测） */
} ipc_shm_state_t;

/**
 * @brief 共享内存块控制结构
 *
 * 每个共享内存块都有独立的引用计数。
 * 当引用计数降至 0 时自动回收至空闲池。
 */
typedef struct ipc_shm_block {
    void*           ptr;       /**< 指向实际内存的指针 */
    size_t          size;      /**< 块大小（字节） */
    atomic_t        ref_count; /**< 引用计数（原子操作） */
    ipc_shm_state_t state;     /**< 当前状态 */
    uint32_t        alloc_id;  /**< 分配 ID（用于调试和验证） */
    struct k_mutex  lock;      /**< 保护块状态的互斥锁 */
} ipc_shm_block_t;

/**
 * @brief 共享内存池管理结构
 *
 * 嵌入到 ipc_service_t 中，由 IPC 服务实例管理。
 */
typedef struct ipc_shm_pool {
    ipc_shm_block_t blocks[CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE]; /**< 静态分配的块数组 */
    uint8_t         mem_pool[CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE *
                     CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE]; /**< 实际内存池 */
    struct k_mutex  pool_lock;                                                 /**< 保护池分配的互斥锁 */
    uint32_t        alloc_counter;                                             /**< 分配计数器（用于生成唯一 ID） */
    atomic_t        active_count; /**< 当前活跃（已分配）块数量（原子：release 末路与 alloc 无需反向嵌套锁） */
    uint32_t        peak_count;   /**< 峰值活跃块数量 */
} ipc_shm_pool_t;

/**
 * @brief 共享内存句柄（不透明类型）
 *
 * 实际上是内存块索引的编码值，不是直接指针。
 * 这样设计可以防止用户直接使用句柄作为指针。
 */
typedef uint32_t ipc_shm_handle_t;

/**
 * @brief 无效句柄常量
 */
#define IPC_SHM_HANDLE_INVALID ((ipc_shm_handle_t) 0xFFFFFFFFU)

/**
 * @brief 初始化共享内存池
 *
 * @param service IPC 服务实例指针
 * @return 0 成功，负值错误码失败
 *
 * SIL-2: 验证所有输入参数
 */
int ipc_shm_init(struct ipc_service* service);

/**
 * @brief 反初始化共享内存池
 *
 * 检查是否有未释放的块并记录警告。
 *
 * @param service IPC 服务实例指针
 *
 * SIL-2: 安全清理，检查泄漏
 *
 * @note 仅打印泄漏诊断；不强制回收仍被持有的块（依赖 ipc_shm_release 配对）
 */
void ipc_shm_deinit(struct ipc_service* service);

/**
 * @brief 从池中分配一个共享内存块
 *
 * 分配的块初始引用计数为 1（调用者持有）。
 * 调用者必须负责在使用完毕后释放。
 *
 * @param service IPC 服务实例指针
 * @param size 请求的大小（必须 <= CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE）
 * @return 有效的句柄，或 IPC_SHM_HANDLE_INVALID（失败）
 *
 * SIL-2:
 * - 验证 service 和 size 参数
 * - 检查池是否已满
 * - 返回错误码而非断言
 */
ipc_shm_handle_t ipc_shm_alloc(struct ipc_service* service, size_t size);

/**
 * @brief 增加共享内存块的引用计数
 *
 * 当需要将块传递给另一个消费者时调用。
 * 每次 acquire 必须有对应的 release。
 *
 * @param service IPC 服务实例指针
 * @param handle 共享内存句柄
 *
 * @note 引用计数用原子更新；块状态与合法性由块互斥锁 ipc_shm_block_t::lock 保护
 * @note 如果句柄无效则静默忽略（SIL-2 防御性编程）
 */
void ipc_shm_acquire(struct ipc_service* service, ipc_shm_handle_t handle);

/**
 * @brief 减少共享内存块的引用计数
 *
 * 当引用计数降至 0 时，块自动回收至空闲池。
 *
 * @param service IPC 服务实例指针
 * @param handle 共享内存句柄
 * @return 0 成功；-EINVAL 句柄无效；-EBUSY 块状态异常
 *
 * SIL-2:
 * - 验证句柄有效性
 * - 检查引用计数不会下溢
 * - 验证块状态一致性
 */
int ipc_shm_release(struct ipc_service* service, ipc_shm_handle_t handle);

/**
 * @brief 获取共享内存块的实际指针
 *
 * @param service IPC 服务实例指针
 * @param handle 共享内存句柄
 * @return 指向实际内存的指针，句柄无效时返回 NULL
 *
 * SIL-2: 验证句柄有效性
 */
void* ipc_shm_get_ptr(struct ipc_service* service, ipc_shm_handle_t handle);

/**
 * @brief 获取共享内存块的大小
 *
 * @param service IPC 服务实例指针
 * @param handle 共享内存句柄
 * @return 块大小（字节），句柄无效时返回 0
 *
 * SIL-2: 验证句柄有效性
 */
size_t ipc_shm_get_size(struct ipc_service* service, ipc_shm_handle_t handle);

/**
 * @brief 验证句柄是否有效且可访问
 *
 * @param service IPC 服务实例指针
 * @param handle 共享内存句柄
 * @return true 有效，false 无效
 */
bool ipc_shm_is_valid(struct ipc_service* service, ipc_shm_handle_t handle);

/**
 * @brief 根据池内指针查找共享内存句柄
 *
 * 当 service_func 通过 ipc_shm_alloc() 返回 out_data 时，框架用此函数解析句柄。
 *
 * @param service IPC 服务实例指针
 * @param ptr 指向池内已分配块的指针
 * @return 有效句柄；ptr 不在池中或无效时返回 IPC_SHM_HANDLE_INVALID
 */
ipc_shm_handle_t ipc_shm_lookup_handle_by_ptr(struct ipc_service* service, const void* ptr);

/**
 * @brief 获取共享内存池统计信息
 *
 * @param service IPC 服务实例指针
 * @param out_active_count 输出：当前活跃块数（可为 NULL）
 * @param out_peak_count 输出：峰值活跃块数（可为 NULL）
 * @param out_free_count 输出：当前空闲块数（可为 NULL）
 *
 * SIL-2: 验证所有输出指针
 */
void ipc_shm_get_stats(struct ipc_service* service, uint32_t* out_active_count, uint32_t* out_peak_count,
                       uint32_t* out_free_count);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_THREAD_IPC_SERVICE_SHARED_MEM */

#endif /* ZEPHYR_IPC_SHARED_MEM_H_ */
