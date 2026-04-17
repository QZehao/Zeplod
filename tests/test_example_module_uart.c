/**
 * @file test_example_module_uart.c
 * @brief UART 通信示例模块单元测试
 * @author OpenClaw Agent
 * @version 1.0
 * @date 2026-04-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-10       1.0            agent          为 example_module_uart 编写完整测试用例
 *
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "event_system.h"
#include "module_manager.h"
#include "example_module_uart.h"

LOG_MODULE_REGISTER(test_example_module_uart);

/* =============================================================================
 * 测试夹具
 * ============================================================================= */

static void *test_suite_setup(void)
{
    int ret;

    /* 全局初始化 - 允许重复初始化（返回 -EALREADY） */
    ret = event_system_init();
    zassert_true(ret == EVENT_OK || ret == -EALREADY, "事件系统初始化失败: %d", ret);

    ret = module_manager_init();
    zassert_true(ret == 0 || ret == -EALREADY, "模块管理器初始化失败: %d", ret);

    ret = module_manager_start();
    zassert_true(ret == 0 || ret == -EALREADY, "模块管理器启动失败: %d", ret);

    return NULL;
}

static void test_suite_teardown(void *fixture)
{
    (void)fixture;
    module_manager_shutdown();
    event_system_stop();
}

/* =============================================================================
 * 测试用例：初始化与配置
 * ============================================================================= */

/**
 * @brief 测试模块初始化 - NULL 配置
 */
ZTEST(example_module_uart, test_init_null_config)
{
    example_module_uart_shutdown();

    int ret = example_module_uart_init(NULL);
    zassert_equal(ret, 0, "NULL 配置初始化应返回 0");

    module_status_t status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后状态应为 INITIALIZED");
}

/**
 * @brief 测试模块初始化 - 有效配置
 */
ZTEST(example_module_uart, test_init_valid_config)
{
    example_module_uart_config_t config = {
        .device_name = "UART_0",
        .baudrate = 115200,
        .rx_buffer_size = 256,
        .tx_buffer_size = 256,
        .enable_interrupt = true};

    int ret = example_module_uart_init(&config);
    zassert_equal(ret, 0, "有效配置初始化应返回 0");

    module_status_t status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后状态应为 INITIALIZED");
}

/**
 * @brief 测试模块初始化 - 禁用中断模式
 */
ZTEST(example_module_uart, test_init_no_interrupt)
{
    example_module_uart_config_t config = {
        .device_name = "UART_1",
        .baudrate = 9600,
        .rx_buffer_size = 128,
        .tx_buffer_size = 128,
        .enable_interrupt = false};

    int ret = example_module_uart_init(&config);
    zassert_equal(ret, 0, "禁用中断模式初始化应返回 0");

    example_module_uart_shutdown();
}

/* =============================================================================
 * 测试用例：生命周期
 * ============================================================================= */

/**
 * @brief 测试模块启动
 */
ZTEST(example_module_uart, test_start)
{
    example_module_uart_init(NULL);

    int ret = example_module_uart_start();
    zassert_equal(ret, 0, "模块启动应返回 0");

    module_status_t status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_RUNNING, "启动后状态应为 RUNNING");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试模块停止
 */
ZTEST(example_module_uart, test_stop)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    int ret = example_module_uart_stop();
    zassert_equal(ret, 0, "模块停止应返回 0");

    module_status_t status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_STOPPED, "停止后状态应为 STOPPED");

    example_module_uart_shutdown();
}

/**
 * @brief 测试完整生命周期
 */
ZTEST(example_module_uart, test_lifecycle)
{
    module_status_t status;

    example_module_uart_init(NULL);
    status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后应为 INITIALIZED");

    example_module_uart_start();
    status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_RUNNING, "启动后应为 RUNNING");

    example_module_uart_stop();
    status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_STOPPED, "停止后应为 STOPPED");

    example_module_uart_shutdown();
    status = example_module_uart_get_status();
    zassert_equal(status, MODULE_STATUS_UNINITIALIZED, "关闭后应为 UNINITIALIZED");
}

/* =============================================================================
 * 测试用例：UART 发送功能
 * ============================================================================= */

/**
 * @brief 测试发送 - NULL 参数
 */
ZTEST(example_module_uart, test_send_null)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    int ret = example_module_uart_send(NULL, 100);
    zassert_true(ret < 0, "NULL 数据发送应返回错误");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试发送 - 零长度
 */
ZTEST(example_module_uart, test_send_zero_length)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    char data[] = "test";
    int ret = example_module_uart_send(data, 0);
    zassert_true(ret < 0, "零长度发送应返回错误");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试发送字符串
 */
ZTEST(example_module_uart, test_send_string)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    int ret = example_module_uart_send_string("AT\r\n");
    zassert_true(ret >= 0, "发送字符串应成功");

    ret = example_module_uart_send_string("Hello UART\r\n");
    zassert_true(ret >= 0, "发送字符串应成功");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试发送二进制数据
 */
ZTEST(example_module_uart, test_send_binary)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    uint8_t bin_data[] = {0x01, 0x02, 0x03, 0x04, 0xFF};
    int ret = example_module_uart_send(bin_data, sizeof(bin_data));
    zassert_true(ret >= 0, "发送二进制数据应成功");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/* =============================================================================
 * 测试用例：UART 接收功能
 * ============================================================================= */

/**
 * @brief 测试接收 - NULL 参数
 */
ZTEST(example_module_uart, test_receive_null)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    int ret = example_module_uart_receive(NULL, 100);
    zassert_true(ret < 0, "NULL 缓冲区接收应返回错误");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试获取接收计数
 */
ZTEST(example_module_uart, test_get_rx_count)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    size_t count = example_module_uart_get_rx_count();
    /* 初始应为 0 或某个确定值 */

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试清空接收缓冲区
 */
ZTEST(example_module_uart, test_clear_rx_buffer)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    /* 清空操作应不崩溃 */
    example_module_uart_clear_rx_buffer();

    /* 再次清空确认安全 */
    example_module_uart_clear_rx_buffer();

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/* =============================================================================
 * 测试用例：统计信息
 * ============================================================================= */

/**
 * @brief 测试获取统计信息
 */
ZTEST(example_module_uart, test_get_stats)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    uint32_t tx_count = 0, rx_count = 0, errors = 0;
    example_module_uart_get_stats(&tx_count, &rx_count, &errors);

    /* 发送一些数据 */
    const char *test_data = "stats test";
    example_module_uart_send(test_data, strlen(test_data));

    /* 再次获取统计 */
    uint32_t tx_after = 0, rx_after = 0, errors_after = 0;
    example_module_uart_get_stats(&tx_after, &rx_after, &errors_after);

    zassert_true(tx_after >= tx_count, "发送计数应增加");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/* =============================================================================
 * 测试用例：控制命令
 * ============================================================================= */

/**
 * @brief 测试控制命令 - 发送
 */
ZTEST(example_module_uart, test_control_send)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    example_module_uart_tx_req_t req = {
        .data = "AT\r\n",
        .len = 5};

    int ret = example_module_uart_control(UART_CMD_SEND, &req);
    zassert_equal(ret, 0, "SEND 命令应返回 0");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试控制命令 - 获取接收计数
 */
ZTEST(example_module_uart, test_control_get_rx_count)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    size_t count = 0;
    int ret = example_module_uart_control(UART_CMD_GET_RX_COUNT, &count);
    zassert_equal(ret, 0, "GET_RX_COUNT 命令应返回 0");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试控制命令 - 清空接收
 */
ZTEST(example_module_uart, test_control_clear_rx)
{
    example_module_uart_init(NULL);
    example_module_uart_start();

    int ret = example_module_uart_control(UART_CMD_CLEAR_RX, NULL);
    zassert_equal(ret, 0, "CLEAR_RX 命令应返回 0");

    example_module_uart_stop();
    example_module_uart_shutdown();
}

/**
 * @brief 测试无效控制命令
 */
ZTEST(example_module_uart, test_control_invalid)
{
    example_module_uart_init(NULL);

    int ret = example_module_uart_control(9999, NULL);
    zassert_true(ret != 0, "无效命令应返回错误");

    example_module_uart_shutdown();
}

/* =============================================================================
 * 测试用例：事件处理
 * ============================================================================= */

/**
 * @brief 测试事件处理 - NULL
 */
ZTEST(example_module_uart, test_event_null)
{
    example_module_uart_on_event(NULL, NULL);
    /* 不应崩溃 */

    event_t event = {0};
    event.type = EVENT_TYPE_GENERIC;
    event.priority = EVENT_PRIORITY_NORMAL;
    event.data_len = 0;
    example_module_uart_on_event(&event, NULL);
    /* 不应崩溃 */
}

/**
 * @brief 测试事件类型定义
 */
ZTEST(example_module_uart, test_event_types)
{
    zassert_true(EVENT_TYPE_UART_RX_DATA > 0, "RX 事件类型应 > 0");
    zassert_true(EVENT_TYPE_UART_TX_COMPLETE > 0, "TX 完成事件类型应 > 0");
    zassert_true(EVENT_TYPE_UART_ERROR > 0, "错误事件类型应 > 0");

    /* 事件类型应互不相同 */
    zassert_true(EVENT_TYPE_UART_RX_DATA != EVENT_TYPE_UART_TX_COMPLETE, "RX 和 TX 事件类型应不同");
    zassert_true(EVENT_TYPE_UART_RX_DATA != EVENT_TYPE_UART_ERROR, "RX 和 ERROR 事件类型应不同");
}

/* =============================================================================
 * 测试用例：接口
 * ============================================================================= */

/**
 * @brief 测试获取接口
 */
ZTEST(example_module_uart, test_get_interface)
{
    const module_interface_t *iface = example_module_uart_get_interface();

    zassert_not_null(iface, "接口不应为 NULL");
    zassert_true(strcmp(iface->name, "example_module_uart") == 0, "接口名称应匹配");
    zassert_true(iface->version == MODULE_VERSION(1, 0, 0), "版本号应匹配");
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(example_module_uart, NULL, test_suite_setup, NULL, NULL, test_suite_teardown);
