/**
 * @file example_module_a.h
 * @brief 示例模块 A 头文件
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
#ifndef EXAMPLE_MODULE_A_H
#define EXAMPLE_MODULE_A_H

#include <zeplod/module_base.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 模块配置
 * ============================================================================= */

typedef struct {
    uint32_t sample_rate_ms;
    uint32_t buffer_size;
    bool     enable_filtering;
} example_module_a_config_t;

/* =============================================================================
 * 模块接口（在 .c 文件中实现）
 * ============================================================================= */

/* 模块初始化 */
int example_module_a_init(void* config);

/* 模块启动 */
int example_module_a_start(void);

/* 模块停止 */
int example_module_a_stop(void);

/* 模块关闭 */
int example_module_a_shutdown(void);

/* 事件处理器 */
void example_module_a_on_event(const event_t* event, void* user_data);

/* 获取模块状态 */
module_status_t example_module_a_get_status(void);

/* 模块控制 */
int example_module_a_control(int cmd, void* arg);

/* 获取模块接口 */
const module_interface_t* example_module_a_get_interface(void);

/* =============================================================================
 * 模块专用 API
 * ============================================================================= */

/**
 * @brief 获取最新传感器数据
 * @param data 输出缓冲区
 * @param len 缓冲区长度
 * @return 读取的字节数
 */
int example_module_a_get_data(void* data, size_t len);

/**
 * @brief 设置采样率
 * @param rate_ms 采样率（毫秒）
 * @return 成功返回 0，失败返回负错误码
 */
int example_module_a_set_rate(uint32_t rate_ms);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_MODULE_A_H */
