/**
 * @file example_module_uart.c
 * @brief UART 通信示例模块实现
 *
 * 演示如何使用 Zephyr UART API 进行串口通信。
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

#include "example_module_uart.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>
#include "app_config.h"
#include "event_system.h"
#include "module_manager.h"

LOG_MODULE_REGISTER(example_module_uart, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义
 * ============================================================================= */

#define UART_THREAD_PRIORITY   5
#define UART_THREAD_STACK_SIZE 2048
#define UART_DEFAULT_BAUDRATE  115200

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    example_module_uart_config_t config;
    module_status_t              status;
    struct k_thread              rx_thread;
    K_KERNEL_STACK_MEMBER(rx_stack, UART_THREAD_STACK_SIZE);
    const struct device* dev;
    uint8_t*             rx_buffer;
    uint8_t*             tx_buffer;
    size_t               rx_head;
    size_t               rx_tail;
    size_t               tx_count;
    size_t               rx_count;
    size_t               error_count;
    struct k_spinlock    rx_lock;
    struct k_mutex       tx_lock;
} example_module_uart_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static example_module_uart_cb_t g_module_uart;
static uint8_t                  g_rx_buffer_static[256];
static uint8_t                  g_tx_buffer_static[256];

/* =============================================================================
 * 前向声明
 * ============================================================================= */

static void uart_rx_thread(void* p1, void* p2, void* p3);
static void uart_irq_callback(const struct device* dev, void* user_data);

/* =============================================================================
 * 模块接口实现
 * ============================================================================= */

int example_module_uart_init(void* config) {
    LOG_INF("初始化 UART 模块...");

    memset(&g_module_uart, 0, sizeof(g_module_uart));

    /* 设置配置 */
    if (config != NULL) {
        g_module_uart.config = *(example_module_uart_config_t*) config;
    } else {
        g_module_uart.config.device_name = "UART_0";
        g_module_uart.config.baudrate = UART_DEFAULT_BAUDRATE;
        g_module_uart.config.rx_buffer_size = sizeof(g_rx_buffer_static);
        g_module_uart.config.tx_buffer_size = sizeof(g_tx_buffer_static);
        g_module_uart.config.enable_interrupt = true;
    }

    g_module_uart.rx_buffer = g_rx_buffer_static;
    g_module_uart.tx_buffer = g_tx_buffer_static;
    g_module_uart.status = MODULE_STATUS_INITIALIZED;

    k_mutex_init(&g_module_uart.tx_lock);

    /* 注册事件类型 */
    event_register_type(EVENT_TYPE_UART_RX_DATA, "uart_rx_data");
    event_register_type(EVENT_TYPE_UART_TX_COMPLETE, "uart_tx_complete");
    event_register_type(EVENT_TYPE_UART_ERROR, "uart_error");

    LOG_INF("UART 模块初始化完成");
    return 0;
}

int example_module_uart_start(void) {
    if (g_module_uart.status != MODULE_STATUS_INITIALIZED && g_module_uart.status != MODULE_STATUS_STOPPED) {
        return -1;
    }

    /* 获取 UART 设备 */
#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_UART_USE_ZEPHYR_CONSOLE)
#if DT_HAS_CHOSEN(zephyr_console)
    g_module_uart.dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#else
    LOG_ERR("devicetree 未选择 zephyr_console");
    g_module_uart.status = MODULE_STATUS_ERROR;
    return -1;
#endif
#else
    g_module_uart.dev = device_get_binding(CONFIG_EXAMPLE_MODULE_UART_DEVICE_NAME);
#endif
    if (g_module_uart.dev == NULL) {
        LOG_ERR("找不到 UART 设备");
        g_module_uart.status = MODULE_STATUS_ERROR;
        return -1;
    }

    if (!device_is_ready(g_module_uart.dev)) {
        LOG_ERR("UART 设备未就绪");
        g_module_uart.status = MODULE_STATUS_ERROR;
        return -1;
    }

    /* 若绑定的是 Zephyr console UART，不要抢占其 IRQ 回调，否则会导致 Shell/LOG 无输出。 */
#if IS_ENABLED(CONFIG_EXAMPLE_MODULE_UART_USE_ZEPHYR_CONSOLE)
    if (g_module_uart.config.enable_interrupt) {
        LOG_WRN("UART module shares zephyr_console; disable IRQ hook to keep Shell/LOG output");
        g_module_uart.config.enable_interrupt = false;
    }
#endif

    /* 配置中断接收（仅非 console 共享场景） */
    if (g_module_uart.config.enable_interrupt &&
        uart_irq_callback_user_data_set(g_module_uart.dev, uart_irq_callback, &g_module_uart) == 0) {
        uart_irq_rx_enable(g_module_uart.dev);
    }

    g_module_uart.status = MODULE_STATUS_RUNNING;

    /* 创建接收线程 */
    k_thread_create(&g_module_uart.rx_thread, g_module_uart.rx_stack, K_THREAD_STACK_SIZEOF(g_module_uart.rx_stack),
                    uart_rx_thread, NULL, NULL, NULL, UART_THREAD_PRIORITY, 0, K_FOREVER);

    k_thread_name_set(&g_module_uart.rx_thread, "mod_uart_rx");
    k_thread_start(&g_module_uart.rx_thread);

    LOG_INF("UART 模块已启动：%s @ %dbps", g_module_uart.dev->name, g_module_uart.config.baudrate);
    return 0;
}

int example_module_uart_stop(void) {
    if (g_module_uart.status != MODULE_STATUS_RUNNING) {
        return 0;
    }

    if (g_module_uart.config.enable_interrupt && g_module_uart.dev != NULL) {
        uart_irq_rx_disable(g_module_uart.dev);
    }

    /* SIL-2: 先设置状态让线程自行退出，避免 k_thread_abort 强制终止 */
    g_module_uart.status = MODULE_STATUS_STOPPED;

    /* 等待线程检测到状态变化并自然退出
     * 线程循环中有 k_msleep(10)，等待 100ms 足够
     */
    k_msleep(100);

    LOG_INF("UART 模块已停止");
    return 0;
}

int example_module_uart_shutdown(void) {
    example_module_uart_stop();
    g_module_uart.status = MODULE_STATUS_UNINITIALIZED;
    return 0;
}

void example_module_uart_on_event(const event_t* event, void* user_data) {
    if (event == NULL)
        return;

    switch (event->type) {
    case EVENT_TYPE_UART_TX_COMPLETE:
        LOG_DBG("UART 发送完成");
        break;
    }
}

module_status_t example_module_uart_get_status(void) {
    return g_module_uart.status;
}

int example_module_uart_control(int cmd, void* arg) {
    switch (cmd) {
    case UART_CMD_SEND:
        if (arg == NULL)
            return -1;
        {
            const example_module_uart_tx_req_t* tx_req = (const example_module_uart_tx_req_t*) arg;
            int                                 sent = example_module_uart_send(tx_req->data, tx_req->len);
            return (sent >= 0) ? 0 : sent;
        }

    case UART_CMD_GET_RX_COUNT:
        if (arg == NULL)
            return -1;
        *(size_t*) arg = example_module_uart_get_rx_count();
        return 0;

    case UART_CMD_CLEAR_RX:
        example_module_uart_clear_rx_buffer();
        return 0;

    case UART_CMD_GET_STATS:
        if (arg == NULL)
            return -1;
        {
            uint32_t* stats = (uint32_t*) arg;
            example_module_uart_get_stats(&stats[0], &stats[1], &stats[2]);
        }
        return 0;

    default:
        return -1;
    }
}

/* =============================================================================
 * 模块特定 API
 * ============================================================================= */

int example_module_uart_send(const void* data, size_t len) {
    if (data == NULL || len == 0 || g_module_uart.dev == NULL) {
        return -1;
    }

    k_mutex_lock(&g_module_uart.tx_lock, K_FOREVER);

    const uint8_t* tx_data = (const uint8_t*) data;
    size_t         sent = 0;

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(g_module_uart.dev, tx_data[i]);
        sent++;
    }

    g_module_uart.tx_count += sent;
    k_mutex_unlock(&g_module_uart.tx_lock);

    return (int) sent;
}

int example_module_uart_send_string(const char* str) {
    if (str == NULL)
        return -1;
    return example_module_uart_send(str, strlen(str));
}

int example_module_uart_receive(void* data, size_t len) {
    if (data == NULL || len == 0)
        return -1;

    k_spinlock_key_t key = k_spin_lock(&g_module_uart.rx_lock);
    size_t           available = g_module_uart.rx_head - g_module_uart.rx_tail;
    size_t           capacity = g_module_uart.config.rx_buffer_size;
    if (available > capacity) {
        /* 防御性修正：理论上不会出现，出现时丢弃最旧数据保证边界安全 */
        g_module_uart.rx_tail = g_module_uart.rx_head - capacity;
        available = capacity;
    }
    size_t to_copy = MIN(len, available);

    uint8_t* rx_data = (uint8_t*) data;
    for (size_t i = 0; i < to_copy; i++) {
        rx_data[i] = g_module_uart.rx_buffer[(g_module_uart.rx_tail + i) % g_module_uart.config.rx_buffer_size];
    }

    g_module_uart.rx_tail += to_copy;
    g_module_uart.rx_count += to_copy;

    k_spin_unlock(&g_module_uart.rx_lock, key);
    return (int) to_copy;
}

size_t example_module_uart_get_rx_count(void) {
    k_spinlock_key_t key = k_spin_lock(&g_module_uart.rx_lock);
    size_t           count = g_module_uart.rx_head - g_module_uart.rx_tail;
    if (count > g_module_uart.config.rx_buffer_size) {
        count = g_module_uart.config.rx_buffer_size;
    }
    k_spin_unlock(&g_module_uart.rx_lock, key);
    return count;
}

void example_module_uart_clear_rx_buffer(void) {
    k_spinlock_key_t key = k_spin_lock(&g_module_uart.rx_lock);
    g_module_uart.rx_head = 0;
    g_module_uart.rx_tail = 0;
    k_spin_unlock(&g_module_uart.rx_lock, key);
}

void example_module_uart_get_stats(uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors) {
    if (tx_count != NULL)
        *tx_count = g_module_uart.tx_count;
    if (rx_count != NULL)
        *rx_count = g_module_uart.rx_count;
    if (errors != NULL)
        *errors = g_module_uart.error_count;
}

/* =============================================================================
 * 内部函数
 * ============================================================================= */

static void uart_rx_thread(void* p1, void* p2, void* p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("UART 接收线程已启动");

    while (g_module_uart.status == MODULE_STATUS_RUNNING) {
        /* 检查接收缓冲区 */
        size_t count = example_module_uart_get_rx_count();

        if (count > 0) {
            /* 读取数据并发布事件 */
            uint8_t buffer[64];
            int     received = example_module_uart_receive(buffer, MIN(count, sizeof(buffer)));

            if (received > 0) {
                event_publish_copy(EVENT_TYPE_UART_RX_DATA, EVENT_PRIORITY_NORMAL, buffer, received);
                LOG_DBG("UART 接收 %d 字节", received);
            }
        }

        k_msleep(10);
    }
}

static void uart_irq_callback(const struct device* dev, void* user_data) {
    ARG_UNUSED(dev);
    example_module_uart_cb_t* cb = (example_module_uart_cb_t*) user_data;

    if (uart_irq_update(cb->dev)) {
        while (uart_irq_rx_ready(cb->dev)) {
            uint8_t byte;
            if (uart_fifo_read(cb->dev, &byte, 1) > 0) {
                /* 存入环形缓冲区 */
                k_spinlock_key_t key = k_spin_lock(&cb->rx_lock);
                size_t           cap = cb->config.rx_buffer_size;
                if (cap > 0U) {
                    cb->rx_buffer[cb->rx_head % cap] = byte;
                    cb->rx_head++;
                    if ((cb->rx_head - cb->rx_tail) > cap) {
                        /* 缓冲区满时覆盖最旧字节，避免计数无限增长后越界语义 */
                        cb->rx_tail = cb->rx_head - cap;
                    }
                }
                k_spin_unlock(&cb->rx_lock, key);
            }
        }

        while (uart_irq_tx_complete(cb->dev)) {
            /* 发送完成 */
        }

        if (uart_err_check(cb->dev) != 0) {
            cb->error_count++;
        }
    }
}

/* =============================================================================
 * 模块接口声明
 * ============================================================================= */

DECLARE_MODULE_INTERFACE(example_module_uart);

const module_interface_t* example_module_uart_get_interface(void) {
    return &example_module_uart_interface;
}

static int example_module_uart_auto_register(void) {
    uint32_t                     module_id;
    example_module_uart_config_t uart_cfg = {
        .device_name = CONFIG_EXAMPLE_MODULE_UART_DEVICE_NAME,
        .baudrate = UART_DEFAULT_BAUDRATE,
        .rx_buffer_size = 256,
        .tx_buffer_size = 256,
        .enable_interrupt = true,
    };

    if (module_manager_register(example_module_uart_get_interface(), &uart_cfg, &module_id) != 0) {
        LOG_ERR("module_manager_register example_module_uart failed");
        return -EIO;
    }
    LOG_INF("Registered example_module_uart (id=%u)", module_id);
    return 0;
}

SYS_INIT(example_module_uart_auto_register, POST_KERNEL, APP_INIT_PRIO_MODULE_UART);
