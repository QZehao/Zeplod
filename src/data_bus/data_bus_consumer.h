/**
 * @file data_bus_consumer.h
 * @brief Data Bus 内部分发 API
 *
 * 消费者注册/注销、数据块分发。
 * 分发时实现引用计数拆分（+N），支持统一自动释放。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：删除 COPY 模式，实现 +N 引用拆分
 *
 */

#ifndef DATA_BUS_CONSUMER_H
#define DATA_BUS_CONSUMER_H

#include "data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dispatch a block to all consumers of a channel
 *
 * Called by the dispatcher thread. Handles REF/COPY mode,
 * acquire/release, and COPY stack-temporary safety.
 *
 * @param ch    Channel
 * @param block Block to dispatch (bus holds ref_count == 1)
 */
void data_bus_consumer_dispatch(data_bus_channel_t *ch, data_bus_block_t *block);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_CONSUMER_H */
