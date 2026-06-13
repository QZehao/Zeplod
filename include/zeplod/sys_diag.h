/**
 * @file sys_diag.h
 * @brief 系统诊断服务头文件
 *
 * 聚合堆内存、事件队列与模块管理器健康快照，供 Shell 或远程运维读取。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 */

#ifndef SYS_DIAG_H
#define SYS_DIAG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 类型定义
 * ============================================================================= */

/**
 * @brief 系统健康快照
 *
 * 各字段在对应子系统未启用或未初始化时为 0。
 */
typedef struct {
    uint32_t heap_free_bytes;        /**< 堆/内存池可用字节（CONFIG_SYS_MEMORY_ENABLE） */
    uint32_t heap_used_bytes;        /**< 堆/内存池已用字节 */
    uint32_t event_queue_depth;      /**< 事件队列当前深度 */
    uint32_t event_queue_capacity;   /**< 事件队列容量（CONFIG_EVENT_QUEUE_SIZE） */
    uint32_t event_dropped_count;    /**< 事件丢弃累计数（event_get_statistics） */
    uint32_t module_count;           /**< 已注册模块总数 */
    uint32_t module_running_count;   /**< 运行中模块数（active_modules） */
    uint32_t module_error_count;     /**< 错误状态模块数 */
    uint32_t uptime_ms;              /**< 系统运行时间（k_uptime_get_32） */
} sys_diag_snapshot_t;

/* =============================================================================
 * 核心 API
 * ============================================================================= */

/**
 * @brief 初始化诊断服务
 *
 * 幂等；由 SYS_INIT 在 POST_KERNEL 阶段自动调用。
 *
 * @return 成功返回 0
 */
int sys_diag_init(void);

/**
 * @brief 采集当前健康快照
 *
 * 只读聚合各子系统已有统计 API，不在多个锁之间持有临界区。
 *
 * @param out 输出快照，不能为 NULL
 * @return 成功返回 0；失败返回 -EINVAL
 */
int sys_diag_collect(sys_diag_snapshot_t* out);

/**
 * @brief 将快照格式化为单行文本
 *
 * @param snap 快照指针，不能为 NULL
 * @param buf 输出缓冲
 * @param buf_len 缓冲大小（含结尾 NUL）
 * @return 成功返回 0；-EINVAL 参数无效；-ENOSPC 缓冲不足；-EIO 格式化失败
 */
int sys_diag_format(const sys_diag_snapshot_t* snap, char* buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* SYS_DIAG_H */
