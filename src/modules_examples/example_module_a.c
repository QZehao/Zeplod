/**
 * @file example_module_a.c
 * @brief 示例模块 A 实现
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

#include "example_module_a.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include "app_config.h"
#include "event_system.h"
#include "module_manager.h"

LOG_MODULE_REGISTER(example_module_a, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Internal Definitions
 * ============================================================================= */

#define EXAMPLE_MODULE_A_BUFFER_SIZE       256
#define EXAMPLE_MODULE_A_THREAD_PRIORITY   5
#define EXAMPLE_MODULE_A_THREAD_STACK_SIZE 2048

/* Control commands */
#define CMD_SET_RATE                       1
#define CMD_GET_RATE                       2
#define CMD_RESET                          3

/* =============================================================================
 * Internal Data Structures
 * ============================================================================= */

typedef struct {
    int32_t  value;
    uint32_t timestamp;
    uint8_t  quality;
} sensor_sample_t;

typedef struct {
    example_module_a_config_t config;
    module_status_t           status;
    struct k_thread           thread;
    K_KERNEL_STACK_MEMBER(stack, EXAMPLE_MODULE_A_THREAD_STACK_SIZE);
    struct k_mutex  buffer_lock;
    sensor_sample_t buffer[EXAMPLE_MODULE_A_BUFFER_SIZE];
    uint32_t        write_idx;
    uint32_t        read_idx;
    uint32_t        sample_count;
    uint32_t        error_count;
    bool            data_ready;
} example_module_a_cb_t;

/* =============================================================================
 * Static Variables
 * ============================================================================= */

static example_module_a_cb_t g_module_a;
static uint32_t              g_subscriber_id = 0;

/* =============================================================================
 * Forward Declarations
 * ============================================================================= */

static void module_a_thread_func(void* p1, void* p2, void* p3);
static void publish_sensor_data(int32_t value, uint32_t timestamp);

/* =============================================================================
 * Module Interface Implementation
 * ============================================================================= */

int example_module_a_init(void* config) {
    LOG_INF("Initializing Example Module A...");

    memset(&g_module_a, 0, sizeof(g_module_a));

    /* Set default or provided config */
    if (config != NULL) {
        g_module_a.config = *(example_module_a_config_t*) config;
    } else {
        g_module_a.config.sample_rate_ms = 100;
        g_module_a.config.buffer_size = EXAMPLE_MODULE_A_BUFFER_SIZE;
        g_module_a.config.enable_filtering = true;
    }

    g_module_a.status = MODULE_STATUS_INITIALIZED;
    g_module_a.write_idx = 0;
    g_module_a.read_idx = 0;

    k_mutex_init(&g_module_a.buffer_lock);

    /* Register event types */
    event_register_type(EVENT_TYPE_SENSOR_DATA, "sensor_data");
    event_register_type(EVENT_TYPE_SENSOR_CONFIG, "sensor_config");

    LOG_INF("Example Module A initialized: sample_rate=%dms", g_module_a.config.sample_rate_ms);
    return 0;
}

int example_module_a_start(void) {
    if (g_module_a.status != MODULE_STATUS_INITIALIZED && g_module_a.status != MODULE_STATUS_STOPPED) {
        return -1;
    }

    g_module_a.status = MODULE_STATUS_RUNNING;

    /* Create data acquisition thread */
    k_thread_create(&g_module_a.thread, g_module_a.stack, K_THREAD_STACK_SIZEOF(g_module_a.stack), module_a_thread_func,
                    NULL, NULL, NULL, EXAMPLE_MODULE_A_THREAD_PRIORITY, 0, K_FOREVER);

    k_thread_name_set(&g_module_a.thread, "mod_a_sensor");
    k_thread_start(&g_module_a.thread);

    /* Subscribe to config events */
    event_subscribe(EVENT_TYPE_SENSOR_CONFIG, example_module_a_on_event, &g_module_a, &g_subscriber_id);

    LOG_INF("Example Module A started");
    return 0;
}

int example_module_a_stop(void) {
    if (g_module_a.status != MODULE_STATUS_RUNNING) {
        return 0;
    }

    /* 先设置状态，让线程看到并退出循环 */
    g_module_a.status = MODULE_STATUS_STOPPED;

    /* 等待线程检测到状态变化并自然退出
     * 线程循环中有 k_msleep(10)，所以等待 50ms 足够
     */
    k_msleep(50);

    /* Unsubscribe from events */
    if (g_subscriber_id != 0) {
        event_unsubscribe(EVENT_TYPE_SENSOR_CONFIG, g_subscriber_id);
        g_subscriber_id = 0;
    }

    LOG_INF("Example Module A stopped");
    return 0;
}

int example_module_a_shutdown(void) {
    example_module_a_stop();
    g_module_a.status = MODULE_STATUS_UNINITIALIZED;
    LOG_INF("Example Module A shutdown");
    return 0;
}

void example_module_a_on_event(const event_t* event, void* user_data) {
    if (event == NULL || user_data == NULL) {
        return;
    }

    example_module_a_cb_t* cb = (example_module_a_cb_t*) user_data;

    LOG_DBG("Module A received event: type=%d", event->type);

    switch (event->type) {
    case EVENT_TYPE_SENSOR_CONFIG:
        /* Handle configuration change */
        if (event->data_len >= sizeof(uint32_t)) {
            uint32_t new_rate;
            if (event->flags & EVENT_FLAG_DATA_INLINE) {
                memcpy(&new_rate, event->data.inline_data, sizeof(uint32_t));
            } else {
                new_rate = *(uint32_t*) event->data.ptr;
            }
            cb->config.sample_rate_ms = new_rate;
            LOG_INF("Sensor rate updated to %dms", new_rate);
        }
        break;

    default:
        LOG_DBG("Unhandled event type: %d", event->type);
        break;
    }
}

module_status_t example_module_a_get_status(void) {
    return g_module_a.status;
}

int example_module_a_control(int cmd, void* arg) {
    switch (cmd) {
    case CMD_SET_RATE:
        if (arg == NULL)
            return -1;
        g_module_a.config.sample_rate_ms = *(uint32_t*) arg;
        return 0;

    case CMD_GET_RATE:
        if (arg == NULL)
            return -1;
        *(uint32_t*) arg = g_module_a.config.sample_rate_ms;
        return 0;

    case CMD_RESET:
        k_mutex_lock(&g_module_a.buffer_lock, K_FOREVER);
        g_module_a.write_idx = 0;
        g_module_a.read_idx = 0;
        g_module_a.sample_count = 0;
        g_module_a.error_count = 0;
        k_mutex_unlock(&g_module_a.buffer_lock);
        return 0;

    default:
        return -1;
    }
}

/* =============================================================================
 * Module-specific API
 * ============================================================================= */

int example_module_a_get_data(void* data, size_t len) {
    if (data == NULL || len == 0) {
        return -1;
    }

    size_t samples_to_read = len / sizeof(sensor_sample_t);
    size_t samples_read = 0;

    k_mutex_lock(&g_module_a.buffer_lock, K_FOREVER);

    while (samples_read < samples_to_read && g_module_a.read_idx != g_module_a.write_idx) {
        ((sensor_sample_t*) data)[samples_read] = g_module_a.buffer[g_module_a.read_idx];

        g_module_a.read_idx = (g_module_a.read_idx + 1) % EXAMPLE_MODULE_A_BUFFER_SIZE;
        samples_read++;
    }

    k_mutex_unlock(&g_module_a.buffer_lock);
    return (int) (samples_read * sizeof(sensor_sample_t));
}

int example_module_a_set_rate(uint32_t rate_ms) {
    if (rate_ms == 0) {
        return -1;
    }

    g_module_a.config.sample_rate_ms = rate_ms;
    LOG_INF("Sample rate set to %dms", rate_ms);
    return 0;
}

/* =============================================================================
 * Internal Functions
 * ============================================================================= */

static void module_a_thread_func(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Module A thread started");

    uint32_t last_sample_time = k_uptime_get_32();

    while (g_module_a.status == MODULE_STATUS_RUNNING) {
        uint32_t now = k_uptime_get_32();
        uint32_t elapsed = now - last_sample_time;

        if (elapsed >= g_module_a.config.sample_rate_ms) {
            /* Simulate sensor reading */
            static int32_t simulated_value = 0;
            simulated_value += 100; /* Incrementing value for demo */

            sensor_sample_t sample = {
                .value = simulated_value, .timestamp = now, .quality = 100 /* 100% quality */
            };

            /* Store in buffer */
            k_mutex_lock(&g_module_a.buffer_lock, K_FOREVER);
            g_module_a.buffer[g_module_a.write_idx] = sample;
            g_module_a.write_idx = (g_module_a.write_idx + 1) % EXAMPLE_MODULE_A_BUFFER_SIZE;
            g_module_a.sample_count++;
            k_mutex_unlock(&g_module_a.buffer_lock);

            /* Publish event */
            publish_sensor_data(simulated_value, now);

            last_sample_time = now;
            g_module_a.data_ready = true;

            LOG_DBG("Sample acquired: value=%d, count=%d", simulated_value, g_module_a.sample_count);
        }

        /* Sleep for a short period */
        k_msleep(10);
    }

    LOG_INF("Module A thread stopped");
}

static void publish_sensor_data(int32_t value, uint32_t timestamp) {
    event_t event = {.type = EVENT_TYPE_SENSOR_DATA,
                     .priority = EVENT_PRIORITY_NORMAL,
                     .timestamp = timestamp,
                     .source_id = 1, /* Module A ID */
                     .data_len = sizeof(int32_t),
                     .flags = EVENT_FLAG_DATA_INLINE};

    memcpy(event.data.inline_data, &value, sizeof(int32_t));

    event_publish(&event);
}

/* =============================================================================
 * Module Interface Declaration
 * ============================================================================= */

const module_interface_t example_module_a_interface = {.name = "example_module_a",
                                                       .version = MODULE_VERSION(1, 0, 0),
                                                       .priority = MODULE_PRIORITY_NORMAL,
                                                       .depends_on = NULL,
                                                       .init = example_module_a_init,
                                                       .start = example_module_a_start,
                                                       .stop = example_module_a_stop,
                                                       .shutdown = example_module_a_shutdown,
                                                       .on_event = example_module_a_on_event,
                                                       .get_status = example_module_a_get_status,
                                                       .control = example_module_a_control};

const module_interface_t* example_module_a_get_interface(void) {
    return &example_module_a_interface;
}

#if APP_CONFIG_ENABLE_MODULE_A
static int example_module_a_auto_register(void) {
    uint32_t                  module_id;
    example_module_a_config_t config_a = {.sample_rate_ms = 100, .buffer_size = 256, .enable_filtering = true};

    if (module_manager_register(example_module_a_get_interface(), &config_a, &module_id) != 0) {
        LOG_ERR("module_manager_register example_module_a failed");
        return -EIO;
    }
    LOG_INF("Registered Module A (id=%u)", module_id);
    return 0;
}

SYS_INIT(example_module_a_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_A);
#endif
