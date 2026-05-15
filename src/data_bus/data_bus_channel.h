/**
 * @file data_bus_channel.h
 * @brief Data Bus internal channel-level API
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
