/**
 * @file example_module_b.h
 * @brief 示例模块 B 头文件
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

#ifndef EXAMPLE_MODULE_B_H
#define EXAMPLE_MODULE_B_H

#include "module_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Module Configuration
 * ============================================================================= */

typedef struct {
    uint32_t tx_buffer_size;
    uint32_t rx_buffer_size;
    uint32_t timeout_ms;
} example_module_b_config_t;

/* =============================================================================
 * Module Interface
 * ============================================================================= */

int             example_module_b_init(void* config);
int             example_module_b_start(void);
int             example_module_b_stop(void);
int             example_module_b_shutdown(void);
void            example_module_b_on_event(const event_t* event, void* user_data);
module_status_t example_module_b_get_status(void);
int             example_module_b_control(int cmd, void* arg);

/* Get module interface */
const module_interface_t* example_module_b_get_interface(void);

/* =============================================================================
 * Module-specific API
 * ============================================================================= */

/**
 * @brief Send data through communication channel
 * @param data Data to send
 * @param len Data length
 * @return Number of bytes sent
 */
int example_module_b_send(const void* data, size_t len);

/**
 * @brief Receive data from communication channel
 * @param data Output buffer
 * @param len Buffer length
 * @return Number of bytes received
 */
int example_module_b_receive(void* data, size_t len);

/**
 * @brief Get communication statistics
 * @param tx_count Output: transmitted bytes
 * @param rx_count Output: received bytes
 * @param errors Output: error count
 */
void example_module_b_get_stats(uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_B_H */
