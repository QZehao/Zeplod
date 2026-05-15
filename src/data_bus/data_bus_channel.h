/**
 * @file data_bus_channel.h
 * @brief Data Bus 内部通道级 API
 *
 * 通道创建、销毁、查找、发布。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：适配统一 auto_release 模型
 *
 */

#ifndef DATA_BUS_CHANNEL_H
#define DATA_BUS_CHANNEL_H

#include "data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a pre-allocated channel object
 * @return 0 on success
 */
int data_bus_channel_obj_init(data_bus_channel_t *ch, const char *name);

/**
 * @brief Reset a channel object (clear consumers, drain queue)
 * Does not free the channel object itself.
 */
void data_bus_channel_obj_reset(data_bus_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_CHANNEL_H */
