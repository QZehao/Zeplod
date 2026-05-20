/**
 * @file ipc_service_event.c
 * @brief Thread IPC 与事件系统桥接实现
 *
 * 功能：
 * - 注册事件类型 EVENT_TYPE_THREAD_IPC_RESPONSE
 * - 发布 IPC 处理结果到事件总线
 *
 * 工作流程：
 * 1. 调用 thread_ipc_event_register_types() 注册事件类型
 * 2. IPC 服务处理完成后调用 thread_ipc_event_publish_result()
 * 3. 事件系统通知所有订阅者
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 * 2026-05-09       1.1            zeh            头文件整理
 *
 */

#include "ipc_service_event.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thread_ipc_evt, CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL);

/**
 * @brief 注册 IPC 事件类型
 *
 * @return EVENT_OK 成功，或其他 event_register_type 错误码
 */
event_status_t thread_ipc_event_register_types(void) {
    /* 与订阅方约定的类型名，用于调试与事件过滤 */
    return event_register_type(EVENT_TYPE_THREAD_IPC_RESPONSE, "thread_ipc_response");
}

/**
 * @brief 发布 IPC 处理结果到事件总线
 *
 * 将固定大小的结果结构体复制进事件队列（非零拷贝，适合小载荷）。
 *
 * @param source_id 调用方约定的服务/模块 ID
 * @param request_id 对应 ipc_call_* 的请求 ID
 * @param result service_func 返回值
 * @param priority 事件优先级
 * @return EVENT_OK 成功，或其他 event_publish_copy 错误码
 */
event_status_t thread_ipc_event_publish_result(uint32_t source_id, ipc_request_id_t request_id, int result,
                                               event_priority_t priority) {
    /* 构建事件载荷 */
    thread_ipc_event_result_t payload = {
        .source_id = source_id,
        .request_id = request_id,
        .result = (int32_t) result,
    };

    /* 发布事件（内部会复制 payload） */
    return event_publish_copy(EVENT_TYPE_THREAD_IPC_RESPONSE, priority, &payload, sizeof(payload));
}
