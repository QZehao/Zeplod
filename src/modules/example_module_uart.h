/**
 * @file example_module_uart.h
 * @brief UART 通信示例模块
 *
 * 演示如何使用 Zephyr UART API 进行串口通信。
 *
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXAMPLE_MODULE_UART_H
#define EXAMPLE_MODULE_UART_H

#include <stddef.h>
#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 模块配置
 * ============================================================================= */

typedef struct {
    const char* device_name;      /* UART 设备名称，如 "UART_0" */
    uint32_t    baudrate;         /* 波特率 */
    size_t      rx_buffer_size;   /* 接收缓冲区大小 */
    size_t      tx_buffer_size;   /* 发送缓冲区大小 */
    bool        enable_interrupt; /* 是否启用中断接收 */
} example_module_uart_config_t;

/* =============================================================================
 * 模块接口
 * ============================================================================= */

int             example_module_uart_init(void* config);
int             example_module_uart_start(void);
int             example_module_uart_stop(void);
int             example_module_uart_shutdown(void);
void            example_module_uart_on_event(const event_t* event, void* user_data);
module_status_t example_module_uart_get_status(void);
int             example_module_uart_control(int cmd, void* arg);

/* =============================================================================
 * 模块特定 API
 * ============================================================================= */

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param len 数据长度
 * @return 实际发送字节数
 */
int example_module_uart_send(const void* data, size_t len);

/**
 * @brief 发送字符串
 * @param str 字符串指针
 * @return 实际发送字节数
 */
int example_module_uart_send_string(const char* str);

/**
 * @brief 接收数据
 * @param data 输出缓冲区
 * @param len 缓冲区长度
 * @return 实际接收字节数
 */
int example_module_uart_receive(void* data, size_t len);

/**
 * @brief 获取接收到的数据长度
 * @return 接收缓冲区中的数据字节数
 */
size_t example_module_uart_get_rx_count(void);

/**
 * @brief 清空接收缓冲区
 */
void example_module_uart_clear_rx_buffer(void);

/**
 * @brief 获取统计信息
 * @param tx_count 输出：发送字节数
 * @param rx_count 输出：接收字节数
 * @param errors 输出：错误计数
 */
void example_module_uart_get_stats(uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors);

/* =============================================================================
 * 事件类型定义
 * ============================================================================= */

#define EVENT_TYPE_UART_RX_DATA     40 /* 接收到数据 */
#define EVENT_TYPE_UART_TX_COMPLETE 41 /* 发送完成 */
#define EVENT_TYPE_UART_ERROR       42 /* UART 错误 */

/* =============================================================================
 * 控制命令
 * ============================================================================= */

#define UART_CMD_SEND               1
#define UART_CMD_GET_RX_COUNT       2
#define UART_CMD_CLEAR_RX           3
#define UART_CMD_GET_STATS          4

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_UART_H */
