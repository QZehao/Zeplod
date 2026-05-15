/**
 * @file data_bus_consumer.h
 * @brief Data Bus internal consumer dispatch API
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
