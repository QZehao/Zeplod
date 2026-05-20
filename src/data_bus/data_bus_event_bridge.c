/**
 * @file data_bus_event_bridge.c
 * @brief Data Bus 到事件系统桥接（可选）
 *
 * 在成功的数据总线发布时发送轻量级事件通知。
 * 仅从线程上下文触发（非 ISR）。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-05-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-05-15       2.0            zeh            重构：适配统一 auto_release 模型
 * 2026-05-19       2.1            zeh            SYS_INIT 注册事件类型；publish 失败打日志
 *
 */

#include "data_bus.h"
#include "data_bus_internal.h"
#include "app_config.h"
#include "event_system.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(data_bus_bridge, CONFIG_DATA_BUS_LOG_LEVEL);

static atomic_t g_event_type_registered;

typedef struct {
	char     channel_name[CONFIG_DATA_BUS_CHANNEL_NAME_MAX];
	uint32_t seq;
	uint32_t len;
} data_bus_event_notification_t;

static int data_bus_event_bridge_init(void)
{
	event_status_t st = event_register_type((event_type_t)CONFIG_DATA_BUS_EVENT_TYPE_ID,
						"DATA_BUS_AVAILABLE");

	if (st != EVENT_OK) {
		LOG_ERR("event_register_type(%u) failed: %d", CONFIG_DATA_BUS_EVENT_TYPE_ID, st);
		return -EIO;
	}

	atomic_set(&g_event_type_registered, 1);
	LOG_INF("Data bus event bridge: type %u registered", CONFIG_DATA_BUS_EVENT_TYPE_ID);
	return 0;
}

SYS_INIT(data_bus_event_bridge_init, POST_KERNEL, APP_INIT_PRIO_DATA_BUS + 1);

void data_bus_event_bridge_notify(data_bus_channel_t *ch, uint32_t seq, size_t len)
{
	event_status_t st;

	if (ch == NULL || ch->name == NULL) {
		return;
	}

	if (!atomic_get(&g_event_type_registered)) {
		return;
	}

	data_bus_event_notification_t notification;
	memset(&notification, 0, sizeof(notification));

	/* 安全拷贝，截断并保证 NUL 结尾 */
	size_t name_len = strlen(ch->name);
	size_t copy_len = MIN(name_len, CONFIG_DATA_BUS_CHANNEL_NAME_MAX - 1);
	memcpy(notification.channel_name, ch->name, copy_len);
	notification.channel_name[copy_len] = '\0';

	notification.seq = seq;
	notification.len = (uint32_t)len;

	LOG_DBG("Bridge notify ch='%s' seq=%u len=%u",
		notification.channel_name, seq, (uint32_t)len);

	st = event_publish_copy((event_type_t)CONFIG_DATA_BUS_EVENT_TYPE_ID,
				EVENT_PRIORITY_NORMAL,
				&notification, sizeof(notification));
	if (st != EVENT_OK) {
		LOG_WRN("Bridge publish failed ch='%s' seq=%u: %d",
			notification.channel_name, seq, st);
	}
}
