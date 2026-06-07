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
 * @brief 初始化预分配的通道对象
 * @return 成功返回 0
 */
int data_bus_channel_obj_init(data_bus_channel_t* ch, const char* name, uint32_t flags);

/**
 * @brief 重置通道对象（清空消费者，排空队列）
 * 不释放通道对象本身。
 */
void data_bus_channel_obj_reset(data_bus_channel_t* ch);

/**
 * @brief 排空通道上已入队的块
 * @param run_dispatch true：先分发给消费者再 release；false：仅 release（销毁/重置）
 */
void data_bus_channel_drain_pending(data_bus_channel_t* ch, bool run_dispatch);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BUS_CHANNEL_H */
