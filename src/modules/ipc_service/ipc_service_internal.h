/**
 * @file ipc_service_internal.h
 * @brief IPC 服务内部共享（不对外公开）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#ifndef IPC_SERVICE_INTERNAL_H
#define IPC_SERVICE_INTERNAL_H

#include "ipc_service.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "state_machine.h"
#include "zepl_thread_service.h"

#ifndef IPC_SERVICE_MAX_QUEUE_SIZE
#define IPC_SERVICE_MAX_QUEUE_SIZE 1024U
#endif

#ifndef IPC_SERVICE_MIN_STACK_SIZE
#define IPC_SERVICE_MIN_STACK_SIZE 512U
#endif

#ifndef IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS
#define IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS ZEPL_THREAD_SERVICE_JOIN_TIMEOUT_MS
#endif

#ifndef IPC_SERVICE_MSGQ_TIMEOUT_MS
#define IPC_SERVICE_MSGQ_TIMEOUT_MS ZEPL_THREAD_SERVICE_POLL_TIMEOUT_MS
#endif

static inline bool ipc_timeout_is_zero(k_timeout_t timeout) {
    return K_TIMEOUT_EQ(timeout, K_NO_WAIT);
}

void ipc_service_state_lock(ipc_service_t* service);
void ipc_service_state_unlock(ipc_service_t* service);
void ipc_service_pending_lock(ipc_service_t* service);
void ipc_service_pending_unlock(ipc_service_t* service);
zepl_state_t ipc_service_lifecycle_state_locked(const ipc_service_t* service);

bool ipc_service_is_accepting_requests(ipc_service_t* service);
int  ipc_put_msgq_until_shutdown(struct k_msgq* queue, const void* msg, const atomic_t* shutdown);
void ipc_drain_queued_messages(ipc_service_t* service);

void ipc_service_worker_thread(void* p1, void* p2, void* p3);
void ipc_service_dispatcher_thread(void* p1, void* p2, void* p3);

ipc_pending_request_t* ipc_find_pending_entry(ipc_service_t* service, ipc_request_id_t request_id);
ipc_pending_request_t* ipc_allocate_pending_entry(ipc_service_t* service);
void ipc_init_pending_entry(ipc_service_t* service, ipc_pending_request_t* entry, ipc_request_id_t request_id,
                            struct k_thread* caller_thread, ipc_async_callback_t callback, void* callback_user_data,
                            ipc_future_t* future);
void ipc_release_pending_entry(ipc_service_t* service, ipc_pending_request_t* entry);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
void ipc_release_pending_shm_handle(ipc_service_t* service, ipc_pending_request_t* entry);
#endif

ipc_future_t* ipc_allocate_future(ipc_service_t* service);
void          ipc_release_future(ipc_service_t* service, ipc_future_t* future);
bool          ipc_future_belongs_to_service(const ipc_service_t* service, const ipc_future_t* future);
bool          ipc_future_is_in_free_list(const ipc_service_t* service, const ipc_future_t* future);

#endif /* IPC_SERVICE_INTERNAL_H */
