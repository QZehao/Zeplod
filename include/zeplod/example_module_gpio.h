/**
 * @file example_module_gpio.h
 * @brief GPIO 控制示例模块
 *
 * 演示如何使用 Zephyr GPIO API 控制 LED 和读取按键。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#ifndef EXAMPLE_MODULE_GPIO_H
#define EXAMPLE_MODULE_GPIO_H

#include <zeplod/module_base.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 模块配置
 * ============================================================================= */

typedef struct {
    const char* led_pin;           /* LED 引脚名称，如 "GPIO0_5" */
    const char* button_pin;        /* 按键引脚名称 */
    uint32_t    blink_interval_ms; /* LED 闪烁间隔（毫秒）*/
    bool        enable_button;     /* 是否启用按键检测 */
} example_module_gpio_config_t;

/* =============================================================================
 * 模块接口
 * ============================================================================= */

int                       example_module_gpio_init(void* config);
int                       example_module_gpio_start(void);
int                       example_module_gpio_stop(void);
int                       example_module_gpio_shutdown(void);
void                      example_module_gpio_on_event(const event_t* event, void* user_data);
module_status_t           example_module_gpio_get_status(void);
int                       example_module_gpio_control(int cmd, void* arg);
const module_interface_t* example_module_gpio_get_interface(void);

/* =============================================================================
 * 模块特定 API
 * ============================================================================= */

/**
 * @brief 设置 LED 状态
 * @param on true 点亮，false 熄灭
 * @return 0 成功，负值错误码
 */
int example_module_gpio_set_led(bool on);

/**
 * @brief 切换 LED 状态
 * @return 当前 LED 状态
 */
bool example_module_gpio_toggle_led(void);

/**
 * @brief 获取按键状态
 * @return true 按键按下，false 未按下
 */
bool example_module_gpio_get_button(void);

/**
 * @brief 设置闪烁间隔
 * @param interval_ms 间隔时间（毫秒）
 * @return 0 成功，负值错误码
 */
int example_module_gpio_set_blink_interval(uint32_t interval_ms);

/* =============================================================================
 * 事件类型定义
 * ============================================================================= */

#define EVENT_TYPE_GPIO_BUTTON_PRESSED  30
#define EVENT_TYPE_GPIO_BUTTON_RELEASED 31
#define EVENT_TYPE_GPIO_LED_STATE       32

/* =============================================================================
 * 控制命令
 * ============================================================================= */

#define GPIO_CMD_SET_LED                1
#define GPIO_CMD_TOGGLE_LED             2
#define GPIO_CMD_GET_BUTTON             3
#define GPIO_CMD_SET_BLINK              4

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_GPIO_H */
