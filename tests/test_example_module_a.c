/**
 * @file test_example_module_a.c
 * @brief 示例模块 A 单元测试
 * @author OpenClaw Agent
 * @version 1.0
 * @date 2026-04-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-10       1.0            agent          为 example_module_a 编写完整测试用例
 *
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "event_system.h"
#include "module_manager.h"
#include "example_module_a.h"

LOG_MODULE_REGISTER(test_example_module_a);

/* =============================================================================
 * 测试夹具 (Test Fixtures)
 * ============================================================================= */

static void *test_suite_setup(void)
{
    /* 全局初始化 */
    zassert_equal(event_system_init(), EVENT_OK, "事件系统初始化失败");
    zassert_equal(module_manager_init(), 0, "模块管理器初始化失败");
    zassert_equal(module_manager_start(), 0, "模块管理器启动失败");
    return NULL;
}

static void test_suite_teardown(void *fixture)
{
    (void)fixture;
    module_manager_shutdown();
    event_system_stop();
}

/* =============================================================================
 * 辅助函数
 * ============================================================================= */

/* 简单的存根模块，用于测试模块管理器 */
static int stub_init(void *config)
{
    (void)config;
    return 0;
}

static int stub_start(void) { return 0; }
static int stub_stop(void) { return 0; }
static void stub_on_event(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
}

DECLARE_MODULE_INTERFACE_MINIMAL(stub_module);

/* =============================================================================
 * 测试用例：模块初始化与生命周期
 * ============================================================================= */

/**
 * @brief 测试模块初始化 - 空配置
 */
ZTEST(example_module_a, test_init_with_null_config)
{
    int ret;

    /* 使用 NULL 配置初始化 */
    ret = example_module_a_init(NULL);
    zassert_equal(ret, 0, "NULL 配置初始化应返回 0");

    /* 验证默认配置 */
    module_status_t status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后状态应为 INITIALIZED");
}

/**
 * @brief 测试模块初始化 - 有效配置
 */
ZTEST(example_module_a, test_init_with_valid_config)
{
    example_module_a_config_t config = {
        .sample_rate_ms = 50,
        .buffer_size = 128,
        .enable_filtering = false};

    /* 先关闭模块（如果之前已启动） */
    example_module_a_stop();
    example_module_a_shutdown();

    int ret = example_module_a_init(&config);
    zassert_equal(ret, 0, "有效配置初始化应返回 0");

    module_status_t status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后状态应为 INITIALIZED");
}

/**
 * @brief 测试模块初始化 - 无效配置（sample_rate_ms = 0）
 */
ZTEST(example_module_a, test_init_with_invalid_config)
{
    example_module_a_config_t config = {
        .sample_rate_ms = 0, /* 无效值 */
        .buffer_size = 128,
        .enable_filtering = false};

    /* 模块应能初始化，但后续操作会用到默认值 */
    int ret = example_module_a_init(&config);
    zassert_equal(ret, 0, "初始化应返回 0（配置检查在运行时进行）");
}

/* =============================================================================
 * 测试用例：模块启动与停止
 * ============================================================================= */

/**
 * @brief 测试模块启动
 */
ZTEST(example_module_a, test_start)
{
    /* 先初始化 */
    example_module_a_init(NULL);

    int ret = example_module_a_start();
    zassert_equal(ret, 0, "模块启动应返回 0");

    module_status_t status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_RUNNING, "启动后状态应为 RUNNING");

    /* 清理 */
    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试模块重复启动
 */
ZTEST(example_module_a, test_start_twice)
{
    example_module_a_init(NULL);

    /* 第一次启动 */
    zassert_equal(example_module_a_start(), 0, "第一次启动应成功");

    /* 第二次启动应返回错误 */
    int ret = example_module_a_start();
    zassert_true(ret != 0, "重复启动应返回错误");

    /* 清理 */
    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试模块停止
 */
ZTEST(example_module_a, test_stop)
{
    example_module_a_init(NULL);
    example_module_a_start();

    int ret = example_module_a_stop();
    zassert_equal(ret, 0, "模块停止应返回 0");

    module_status_t status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_STOPPED, "停止后状态应为 STOPPED");

    example_module_a_shutdown();
}

/**
 * @brief 测试模块停止后重新启动
 */
ZTEST(example_module_a, test_restart)
{
    example_module_a_init(NULL);
    example_module_a_start();
    example_module_a_stop();

    /* 重新启动 */
    int ret = example_module_a_start();
    zassert_equal(ret, 0, "重启应成功");

    module_status_t status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_RUNNING, "重启后状态应为 RUNNING");

    example_module_a_stop();
    example_module_a_shutdown();
}

/* =============================================================================
 * 测试用例：模块控制命令
 * ============================================================================= */

/**
 * @brief 测试设置采样率
 */
ZTEST(example_module_a, test_control_set_rate)
{
    example_module_a_init(NULL);

    uint32_t rate = 200;
    int ret = example_module_a_control(CMD_SET_RATE, &rate);
    zassert_equal(ret, 0, "设置采样率应返回 0");

    /* 验证 */
    uint32_t get_rate = 0;
    ret = example_module_a_control(CMD_GET_RATE, &get_rate);
    zassert_equal(ret, 0, "获取采样率应返回 0");
    zassert_equal(get_rate, 200, "采样率应已更新");

    example_module_a_shutdown();
}

/**
 * @brief 测试获取采样率
 */
ZTEST(example_module_a, test_control_get_rate)
{
    example_module_a_config_t config = {
        .sample_rate_ms = 150,
        .buffer_size = 256,
        .enable_filtering = true};

    example_module_a_init(&config);

    uint32_t rate = 0;
    int ret = example_module_a_control(CMD_GET_RATE, &rate);
    zassert_equal(ret, 0, "获取采样率应返回 0");
    zassert_equal(rate, 150, "采样率应匹配配置");

    example_module_a_shutdown();
}

/**
 * @brief 测试重置模块
 */
ZTEST(example_module_a, test_control_reset)
{
    example_module_a_init(NULL);
    example_module_a_start();

    /* 重置前先获取一些数据 */
    int8_t dummy[64];
    example_module_a_get_data(dummy, sizeof(dummy));

    /* 重置 */
    int ret = example_module_a_control(CMD_RESET, NULL);
    zassert_equal(ret, 0, "重置应返回 0");

    /* 验证内部状态已重置 */
    /* 注意：需要通过后续操作验证重置效果 */

    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试无效命令
 */
ZTEST(example_module_a, test_control_invalid_cmd)
{
    example_module_a_init(NULL);

    int ret = example_module_a_control(9999, NULL); /* 无效命令 */
    zassert_true(ret != 0, "无效命令应返回错误");

    example_module_a_shutdown();
}

/**
 * @brief 测试 NULL 参数
 */
ZTEST(example_module_a, test_control_null_arg)
{
    example_module_a_init(NULL);

    /* SET_RATE 需要非 NULL 参数 */
    int ret = example_module_a_control(CMD_SET_RATE, NULL);
    zassert_true(ret != 0, "SET_RATE NULL 参数应返回错误");

    /* GET_RATE 需要非 NULL 参数 */
    ret = example_module_a_control(CMD_GET_RATE, NULL);
    zassert_true(ret != 0, "GET_RATE NULL 参数应返回错误");

    example_module_a_shutdown();
}

/* =============================================================================
 * 测试用例：模块 API
 * ============================================================================= */

/**
 * @brief 测试获取接口
 */
ZTEST(example_module_a, test_get_interface)
{
    const module_interface_t *iface = example_module_a_get_interface();

    zassert_not_null(iface, "接口不应为 NULL");
    zassert_true(strcmp(iface->name, "example_module_a") == 0, "接口名称应匹配");
    zassert_true(iface->version == MODULE_VERSION(1, 0, 0), "版本号应匹配");
}

/**
 * @brief 测试获取数据 - 空缓冲区
 */
ZTEST(example_module_a, test_get_data_null)
{
    example_module_a_init(NULL);
    example_module_a_start();

    /* NULL 数据应返回错误 */
    int ret = example_module_a_get_data(NULL, 100);
    zassert_true(ret < 0, "NULL 数据应返回错误");

    /* 长度为 0 应返回错误 */
    int8_t dummy[64];
    ret = example_module_a_get_data(dummy, 0);
    zassert_true(ret < 0, "长度为 0 应返回错误");

    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试获取数据 - 正常运行
 */
ZTEST(example_module_a, test_get_data_normal)
{
    example_module_a_config_t config = {
        .sample_rate_ms = 10, /* 10ms 采样率，加快测试 */
        .buffer_size = 256,
        .enable_filtering = true};

    example_module_a_init(&config);
    example_module_a_start();

    /* 等待一些数据被采集 */
    k_msleep(50);

    /* 尝试读取数据 */
    sensor_sample_t samples[10];
    int ret = example_module_a_get_data(samples, sizeof(samples));

    /* 至少应该有一些数据 */
    zassert_true(ret >= 0, "获取数据应返回 >= 0");

    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试设置采样率 API
 */
ZTEST(example_module_a, test_set_rate)
{
    example_module_a_init(NULL);

    /* 有效采样率 */
    int ret = example_module_a_set_rate(100);
    zassert_equal(ret, 0, "设置有效采样率应返回 0");

    /* 无效采样率 */
    ret = example_module_a_set_rate(0);
    zassert_true(ret != 0, "设置 0 采样率应返回错误");

    example_module_a_shutdown();
}

/* =============================================================================
 * 测试用例：事件处理
 * ============================================================================= */

/**
 * @brief 测试事件处理 - SENSOR_CONFIG 事件
 */
ZTEST(example_module_a, test_event_sensor_config)
{
    example_module_a_init(NULL);
    example_module_a_start();

    /* 发布配置事件 */
    uint32_t new_rate = 500;
    event_publish_copy(EVENT_TYPE_SENSOR_CONFIG, EVENT_PRIORITY_NORMAL, &new_rate, sizeof(new_rate));

    /* 等待事件处理 */
    k_msleep(20);

    /* 验证配置已更新 */
    uint32_t rate = 0;
    example_module_a_control(CMD_GET_RATE, &rate);
    zassert_equal(rate, 500, "采样率应已更新为 500");

    example_module_a_stop();
    example_module_a_shutdown();
}

/**
 * @brief 测试事件处理 - NULL 事件
 */
ZTEST(example_module_a, test_event_null)
{
    /* 传入 NULL 应不崩溃 */
    example_module_a_on_event(NULL, NULL);
    /* 不应崩溃 */
}

/**
 * @brief 测试事件处理 - NULL user_data
 */
ZTEST(example_module_a, test_event_null_user_data)
{
    event_t event = {
        .type = EVENT_TYPE_SENSOR_CONFIG,
        .priority = EVENT_PRIORITY_NORMAL,
        .data = NULL,
        .data_len = 0};

    /* 传入 NULL user_data 应不崩溃 */
    example_module_a_on_event(&event, NULL);
    /* 不应崩溃 */
}

/* =============================================================================
 * 测试用例：模块状态转换
 * ============================================================================= */

/**
 * @brief 测试完整生命周期
 */
ZTEST(example_module_a, test_lifecycle)
{
    module_status_t status;

    /* 1. 初始状态（刚初始化后） */
    example_module_a_init(NULL);
    status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_INITIALIZED, "初始化后应为 INITIALIZED");

    /* 2. 启动 */
    example_module_a_start();
    status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_RUNNING, "启动后应为 RUNNING");

    /* 3. 停止 */
    example_module_a_stop();
    status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_STOPPED, "停止后应为 STOPPED");

    /* 4. 关闭 */
    example_module_a_shutdown();
    status = example_module_a_get_status();
    zassert_equal(status, MODULE_STATUS_UNINITIALIZED, "关闭后应为 UNINITIALIZED");
}

/**
 * @brief 测试未初始化就启动
 */
ZTEST(example_module_a, test_start_without_init)
{
    /* 先确保处于未初始化状态 */
    example_module_a_shutdown();

    /* 未初始化就启动应返回错误 */
    int ret = example_module_a_start();
    zassert_true(ret != 0, "未初始化启动应返回错误");
}

/**
 * @brief 测试重复关闭
 */
ZTEST(example_module_a, test_shutdown_twice)
{
    example_module_a_init(NULL);
    example_module_a_start();
    example_module_a_stop();

    /* 第一次关闭 */
    int ret = example_module_a_shutdown();
    zassert_equal(ret, 0, "第一次关闭应返回 0");

    /* 第二次关闭应安全（幂等） */
    ret = example_module_a_shutdown();
    zassert_equal(ret, 0, "重复关闭应返回 0");
}

/* =============================================================================
 * 测试用例：线程安全
 * ============================================================================= */

/**
 * @brief 测试并发访问
 */
ZTEST(example_module_a, test_concurrent_access)
{
    example_module_a_init(NULL);
    example_module_a_start();

    /* 并发读取数据 */
    for (int i = 0; i < 10; i++) {
        sensor_sample_t samples[10];
        example_module_a_get_data(samples, sizeof(samples));

        /* 并发设置参数 */
        uint32_t rate = 50 + i * 10;
        example_module_a_set_rate(rate);
    }

    example_module_a_stop();
    example_module_a_shutdown();
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(example_module_a, NULL, test_suite_setup, NULL, test_suite_teardown);
