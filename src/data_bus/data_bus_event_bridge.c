/**
 * @file data_bus_event_bridge.c
 * @brief Data Bus to Event System bridge (optional)
 *
 * Sends lightweight event notifications on successful data bus publishes.
 * Only triggered from thread context (not ISR).
 */

#include "data_bus.h"
#include "data_bus_internal.h"
#include "data_bus_memory.h"
#include "event_system.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>

/* Event type for data bus notifications */
#ifndef EVENT_TYPE_DATA_BUS_AVAILABLE
#define EVENT_TYPE_DATA_BUS_AVAILABLE 30U
#endif

typedef struct {
	char     channel_name[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
	uint32_t seq;
	uint32_t len;
} data_bus_event_notification_t;

/* Forward declaration from event system */
event_status_t event_publish_copy(event_type_t type, event_priority_t priority,
				  const void *data, size_t data_len);

void data_bus_event_bridge_notify(data_bus_channel_t *ch, uint32_t seq, size_t len)
{
	if (ch == NULL || ch->name == NULL) {
		return;
	}

	data_bus_event_notification_t notification;
	memset(&notification, 0, sizeof(notification));

	/* Safe copy with truncation and NUL termination */
	size_t name_len = strlen(ch->name);
	size_t copy_len = MIN(name_len, CONFIG_DATA_BUS_CHANNEL_NAME_MAX - 1);
	memcpy(notification.channel_name, ch->name, copy_len);
	notification.channel_name[copy_len] = '\0';

	notification.seq = seq;
	notification.len = (uint32_t)len;

	event_publish_copy(EVENT_TYPE_DATA_BUS_AVAILABLE,
			   EVENT_PRIORITY_NORMAL,
			   &notification, sizeof(notification));
}
