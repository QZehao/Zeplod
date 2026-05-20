/**
 * @file ipc_service_event.h
 * @brief 可选桥接模块：Thread IPC Service → Event System
 *
 * 功能说明：
 * - 将 IPC 服务的处理结果发布到事件总线
 * - 其他模块可通过订阅事件来获取 IPC 处理结果
 * - 实现模块间松耦合通信
 *
 * 配置要求：
 * - CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE=y
 * - 需要 EVENT_SYSTEM 支持
 *
 * 典型用法：
 * 1. 在系统初始化时调用 thread_ipc_event_register_types()
 * 2. 在 IPC 服务函数末尾调用 thread_ipc_event_publish_result()
 * 3. 其他模块订阅 EVENT_TYPE_THREAD_IPC_RESPONSE 事件
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 * 2026-05-09       1.1            zeh            register_types 幂等说明；头文件整理
 *
 */

#ifndef IPC_SERVICE_EVENT_H_
#define IPC_SERVICE_EVENT_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE)

#include "event_system.h"
#include "ipc_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IPC 事件结果载荷结构
 *
 * 随事件分发的固定大小数据结构，可复制进事件队列。
 *
 * @note 此结构设计为小尺寸，适合直接复制到事件队列
 * @note 如需传递大数据，请另发事件类型或自行在 service_func 内
 *       调用 event_publish_copy，并自行保证 data 指针生命周期
 */
typedef struct {
    uint32_t         source_id;  /**< 调用方约定的服务/模块 ID */
    ipc_request_id_t request_id; /**< 对应 ipc_call_* 的请求 ID */
    int32_t          result;     /**< service_func 返回值 */
} thread_ipc_event_result_t;

/**
 * @brief 注册桥接使用的事件类型
 *
 * @return EVENT_OK 成功，或 event_register_type 错误码
 *
 * @note 此函数是幂等的，可多次调用（委托 event_register_type：同类型 ID 已注册时仍返回 EVENT_OK）
 * @note 应在系统初始化时调用一次
 */
event_status_t thread_ipc_event_register_types(void);

/**
 * @brief 将 IPC 处理结果发布到事件总线
 *
 * 内部使用 event_publish_copy 创建事件副本。
 *
 * @param source_id 来源 ID（如模块 ID 或固定魔数）
 * @param request_id 当前请求 ID
 * @param result 服务处理结果，将写入 payload.result
 * @param priority 事件优先级
 * @return EVENT_OK 成功，或其他 event_publish_copy 错误码
 *
 * @note 典型用法：在 ipc_service_func_t 末尾、return 前调用
 * @note 供其他模块订阅 EVENT_TYPE_THREAD_IPC_RESPONSE 事件
 */
event_status_t thread_ipc_event_publish_result(uint32_t source_id, ipc_request_id_t request_id, int result,
                                               event_priority_t priority);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE */

#endif /* IPC_SERVICE_EVENT_H_ */
