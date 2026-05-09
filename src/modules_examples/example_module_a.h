/**
 * @file example_module_a.h
 * @brief 示例模块 A 头文件
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
#ifndef EXAMPLE_MODULE_A_H
#define EXAMPLE_MODULE_A_H

#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Module Configuration
 * ============================================================================= */

typedef struct {
    uint32_t sample_rate_ms;
    uint32_t buffer_size;
    bool     enable_filtering;
} example_module_a_config_t;

/* =============================================================================
 * Module Interface (to be implemented in .c file)
 * ============================================================================= */

/* Module initialization */
int example_module_a_init(void* config);

/* Module start */
int example_module_a_start(void);

/* Module stop */
int example_module_a_stop(void);

/* Module shutdown */
int example_module_a_shutdown(void);

/* Event handler */
void example_module_a_on_event(const event_t* event, void* user_data);

/* Get module status */
module_status_t example_module_a_get_status(void);

/* Module control */
int example_module_a_control(int cmd, void* arg);

/* Get module interface */
const module_interface_t* example_module_a_get_interface(void);

/* =============================================================================
 * Module-specific API
 * ============================================================================= */

/**
 * @brief Get latest sensor data
 * @param data Output buffer
 * @param len Buffer length
 * @return Number of bytes read
 */
int example_module_a_get_data(void* data, size_t len);

/**
 * @brief Set sampling rate
 * @param rate_ms Sampling rate in milliseconds
 * @return 0 on success, negative error code on failure
 */
int example_module_a_set_rate(uint32_t rate_ms);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_A_H */
