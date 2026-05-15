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
 * @brief 线程上下文分配（优先 slab，k_malloc 兜底）
 *
 * 两步分配：
 *   1. 从 data_bus_block_slab 分配 data_bus_block_t
 *   2. 从数据 slab 分配数据缓冲区（或 k_malloc）
 *
 * @return ref_count == 0（尚未进入生命周期）的块，失败返回 NULL
 */
data_bus_block_t* data_bus_mem_alloc(size_t len);

/**
 * @brief ISR 上下文分配（仅 slab，无兜底）
 *
 * 同上，但步骤 2 仅使用 slab（K_NO_WAIT）。
 * slab 耗尽或长度超过最大 slab 大小时返回 NULL。
 */
data_bus_block_t* data_bus_mem_alloc_isr(size_t len);

/**
 * @brief 直接释放（仅用于尚未进入引用计数生命周期的块）
 *
 * 释放结构体（slab/k_free）和数据缓冲区（slab/k_free）。
 * 在发布失败、尚未进入队列时用于回滚。
 */
void data_bus_mem_free(data_bus_block_t* block);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_MEMORY_H */
