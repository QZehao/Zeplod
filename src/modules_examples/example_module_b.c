/**
 * @file example_module_b.c
 * @brief 示例模块 B 实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include "example_module_b.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include "app_config.h"
#include "event_system.h"
#include "module_manager.h"

LOG_MODULE_REGISTER(example_module_b, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#define EXAMPLE_MODULE_B_THREAD_PRIORITY   6
#define EXAMPLE_MODULE_B_THREAD_STACK_SIZE 2048

/* Control commands */
#define CMD_SEND_DATA                      1
#define CMD_GET_STATS                      2
#define CMD_CLEAR_BUFFERS                  3

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    example_module_b_config_t config;
    module_status_t           status;
    struct k_thread           thread;
    K_KERNEL_STACK_MEMBER(stack, EXAMPLE_MODULE_B_THREAD_STACK_SIZE);
    uint32_t subscriber_id;
    uint32_t event_type;
} example_module_b_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static example_module_b_cb_t g_module_b;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void module_b_thread_func(void* p1, void* p2, void* p3);
static void on_sensor_data(const event_t* event, void* user_data);

/* =============================================================================
 * Module Interface Implementation
 * ============================================================================= */

int example_module_b_init(void* config) {
    LOG_INF("Initializing Example Module B...");

    if (config != NULL) {
        memcpy(&g_module_b.config, config, sizeof(example_module_b_config_t));
    } else {
        /* Default configuration */
        g_module_b.config.tx_buffer_size = 512;
        g_module_b.config.rx_buffer_size = 512;
        g_module_b.config.timeout_ms = 1000;
    }

    g_module_b.status = MODULE_STATUS_INITIALIZED;
    g_module_b.subscriber_id = 0;
    g_module_b.event_type = EVENT_TYPE_SENSOR_DATA;

    LOG_INF("Module B initialized");
    return 0;
}

int example_module_b_start(void) {
    LOG_INF("Starting Module B...");

    if (g_module_b.status != MODULE_STATUS_INITIALIZED && g_module_b.status != MODULE_STATUS_STOPPED) {
        LOG_ERR("Module not initialized");
        return -1;
    }

    /* Subscribe to sensor data events from module A */
    g_module_b.event_type = EVENT_TYPE_SENSOR_DATA;
    event_subscribe(g_module_b.event_type, on_sensor_data, &g_module_b, &g_module_b.subscriber_id);

    /* Start module thread */
    k_thread_create(&g_module_b.thread, g_module_b.stack, K_THREAD_STACK_SIZEOF(g_module_b.stack), module_b_thread_func,
                    &g_module_b, NULL, NULL, EXAMPLE_MODULE_B_THREAD_PRIORITY, 0, K_NO_WAIT);

    g_module_b.status = MODULE_STATUS_RUNNING;
    LOG_INF("Module B started");
    return 0;
}

int example_module_b_stop(void) {
    LOG_INF("Stopping Module B...");

    if (g_module_b.status != MODULE_STATUS_RUNNING) {
        return 0;
    }

    /* SIL-2: 先设置状态让线程自行退出，避免 k_thread_abort 强制终止 */
    g_module_b.status = MODULE_STATUS_STOPPED;

    /* 等待线程检测到状态变化并自然退出
     * 线程循环中有 k_msleep(50)，等待 150ms 足够
     */
    k_msleep(150);

    /* Unsubscribe from events */
    if (g_module_b.subscriber_id != 0) {
        event_unsubscribe(g_module_b.event_type, g_module_b.subscriber_id);
        g_module_b.subscriber_id = 0;
    }

    LOG_INF("Module B stopped");
    return 0;
}

int example_module_b_shutdown(void) {
    LOG_INF("Shutting down Module B...");

    example_module_b_stop();
    g_module_b.status = MODULE_STATUS_UNINITIALIZED;

    LOG_INF("Module B shutdown complete");
    return 0;
}

module_status_t example_module_b_get_status(void) {
    return g_module_b.status;
}

void example_module_b_on_event(const event_t* event, void* user_data) {
    ARG_UNUSED(user_data);

    if (event == NULL) {
        return;
    }

    /* Handle all events generically */
    LOG_DBG("Received event type: %d", event->type);

    if (event->data_len >= sizeof(int32_t)) {
        int32_t sensor_value;
        if (event->flags & EVENT_FLAG_DATA_INLINE) {
            memcpy(&sensor_value, event->data.inline_data, sizeof(int32_t));
        } else {
            sensor_value = *(int32_t*) event->data.ptr;
        }
        LOG_DBG("Event data: %d", sensor_value);
    }
}

int example_module_b_control(int cmd, void* arg) {
    switch (cmd) {
    case CMD_GET_STATS:
        /* Could return statistics */
        return 0;

    case CMD_CLEAR_BUFFERS:
        /* Could clear buffers */
        return 0;

    default:
        return -1;
    }
}

/* =============================================================================
 * Module-specific API
 * ============================================================================= */

int example_module_b_send(const void* data, size_t len) {
    if (data == NULL || len == 0) {
        return -1;
    }

    /* Simulate sending data */
    LOG_DBG("Sending %d bytes", len);
    return (int) len;
}

int example_module_b_receive(void* data, size_t len) {
    if (data == NULL || len == 0) {
        return -1;
    }

    /* Simulate receiving data */
    memset(data, 0, len);
    return 0;
}

void example_module_b_get_stats(uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors) {
    if (tx_count != NULL)
        *tx_count = 0;
    if (rx_count != NULL)
        *rx_count = 0;
    if (errors != NULL)
        *errors = 0;
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void module_b_thread_func(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Module B thread started");

    while (g_module_b.status == MODULE_STATUS_RUNNING) {
        /* Simulate communication processing */
        k_msleep(50);
    }

    LOG_INF("Module B thread stopped");
}

static void on_sensor_data(const event_t* event, void* user_data) {
    ARG_UNUSED(user_data);

    if (event == NULL) {
        return;
    }

    LOG_DBG("Received sensor data");

    /* Process sensor data and potentially send response */
    if (event->data_len >= sizeof(int32_t)) {
        int32_t sensor_value;
        if (event->flags & EVENT_FLAG_DATA_INLINE) {
            memcpy(&sensor_value, event->data.inline_data, sizeof(int32_t));
        } else {
            sensor_value = *(int32_t*) event->data.ptr;
        }

        /* Example: Send acknowledgment */
        uint32_t ack = sensor_value * 2; /* Simple transformation */
        example_module_b_send(&ack, sizeof(ack));

        LOG_DBG("Processed sensor value %d", sensor_value);
    }
}

/* =============================================================================
 * Module Interface Declaration
 * ============================================================================= */

const module_interface_t example_module_b_interface = {.name = "example_module_b",
                                                       .version = MODULE_VERSION(1, 0, 0),
                                                       .priority = MODULE_PRIORITY_NORMAL,
                                                       .depends_on = NULL,
                                                       .init = example_module_b_init,
                                                       .start = example_module_b_start,
                                                       .stop = example_module_b_stop,
                                                       .shutdown = example_module_b_shutdown,
                                                       .on_event = example_module_b_on_event,
                                                       .get_status = example_module_b_get_status,
                                                       .control = example_module_b_control};

const module_interface_t* example_module_b_get_interface(void) {
    return &example_module_b_interface;
}

#if APP_CONFIG_ENABLE_MODULE_B
static int example_module_b_auto_register(void) {
    uint32_t                  module_id;
    example_module_b_config_t config_b = {.tx_buffer_size = 512, .rx_buffer_size = 512, .timeout_ms = 1000};

    if (module_manager_register(example_module_b_get_interface(), &config_b, &module_id) != 0) {
        LOG_ERR("module_manager_register example_module_b failed");
        return -EIO;
    }
    LOG_INF("Registered Module B (id=%u)", module_id);
    return 0;
}

SYS_INIT(example_module_b_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_B);
#endif
