/**
 * @file ipc_service_call.c
 * @brief IPC 服务调用 API（sync/async/future/cancel）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#include "ipc_service_internal.h"

#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_DECLARE(thread_ipc_svc, CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL);

static int ipc_call_sync_impl(ipc_service_t* service, const void* data, size_t data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                              ipc_shm_handle_t in_shm_handle,
#endif
                              void** out_data, size_t* out_data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                              ipc_shm_handle_t* out_shm_handle,
#endif
                              k_timeout_t timeout) {
    if (out_data == NULL || out_data_size == NULL) {
        return -EINVAL;
    }

    /* 输出参数在任何错误返回前都置为确定值，调用方无需依赖自行初始化。 */
    *out_data = NULL;
    *out_data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (out_shm_handle != NULL) {
        *out_shm_handle = 0;
    }
#endif

    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

    if (ipc_timeout_is_zero(timeout)) {
        LOG_WRN("ipc_call_sync called with zero timeout");
        return -EINVAL;
    }

    if (k_current_get() == &service->thread || k_current_get() == &service->response_thread) {
        return -EDEADLK;
    }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (in_shm_handle != 0 && !ipc_shm_is_valid(service, in_shm_handle)) {
        return -EINVAL;
    }
#endif

    ipc_request_id_t request_id = ipc_generate_request_id();

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = ipc_pending_table_alloc(service);

    if (entry == NULL) {
        ipc_service_pending_unlock(service);
        LOG_ERR("No pending slot available");
        return -ENOMEM;
    }

    ipc_pending_table_init_entry(service, entry, request_id, k_current_get(), NULL, NULL, NULL);

    ipc_service_pending_unlock(service);

    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = NULL,
        .callback_user_data = NULL,
        .caller_thread = k_current_get(),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        .shm_handle = in_shm_handle,
#endif
    };

    int ret = ipc_put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        ipc_pending_table_release(service, entry);
        ipc_service_pending_unlock(service);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (in_shm_handle != 0) {
            ipc_shm_release(service, in_shm_handle);
        }
#endif
        return ret;
    }

    ret = k_sem_take(&entry->response_sem, timeout);
    if (ret != 0) {
        ipc_service_pending_lock(service);
        if (entry->in_use) {
            ipc_pending_table_release(service, entry);
        }
        ipc_service_pending_unlock(service);
        return ret;
    }

    ipc_service_pending_lock(service);

    if (!entry->in_use) {
        ipc_service_pending_unlock(service);
        return -ECANCELED;
    }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (entry->shm_handle != 0 && out_shm_handle == NULL) {
        ipc_shm_handle_t leaked = entry->shm_handle;

        entry->shm_handle = 0;
        ipc_pending_table_release(service, entry);
        ipc_service_pending_unlock(service);
        ipc_shm_release(service, leaked);
        LOG_WRN("ipc_call_sync: shm output requires ipc_call_sync_shm");
        return -ENOTSUP;
    }
#endif

    *out_data = (void*) entry->response_data;
    *out_data_size = entry->response_data_size;
    int result = entry->result;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    ipc_shm_handle_t shm_handle = entry->shm_handle;

    entry->shm_handle = 0;
#endif

    ipc_pending_table_release(service, entry);
    ipc_service_pending_unlock(service);

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (out_shm_handle != NULL) {
        *out_shm_handle = shm_handle;
    } else if (shm_handle != 0) {
        ipc_shm_release(service, shm_handle);
    }
#endif

    return result;
}

int ipc_call_sync(ipc_service_t* service, const void* data, size_t data_size, void** out_data, size_t* out_data_size,
                  k_timeout_t timeout) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    return ipc_call_sync_impl(service, data, data_size, 0, out_data, out_data_size, NULL, timeout);
#else
    return ipc_call_sync_impl(service, data, data_size, out_data, out_data_size, timeout);
#endif
}

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
int ipc_call_sync_shm(ipc_service_t* service, const void* data, size_t data_size, ipc_shm_handle_t in_shm_handle,
                      void** out_data, size_t* out_data_size, ipc_shm_handle_t* out_shm_handle, k_timeout_t timeout) {
    return ipc_call_sync_impl(service, data, data_size, in_shm_handle, out_data, out_data_size, out_shm_handle,
                              timeout);
}
#endif

int ipc_call_async(ipc_service_t* service, const void* data, size_t data_size, ipc_async_callback_t callback,
                   void* user_data,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                   ipc_shm_handle_t in_shm_handle,
#endif
                   ipc_request_id_t* out_request_id) {
    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

    if (callback == NULL) {
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (in_shm_handle != 0 && !ipc_shm_is_valid(service, in_shm_handle)) {
        return -EINVAL;
    }
#endif

    ipc_request_id_t request_id = ipc_generate_request_id();

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = ipc_pending_table_alloc(service);

    if (entry == NULL) {
        ipc_service_pending_unlock(service);
        return -ENOMEM;
    }

    ipc_pending_table_init_entry(service, entry, request_id, k_current_get(), callback, user_data, NULL);

    ipc_service_pending_unlock(service);

    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = callback,
        .callback_user_data = user_data,
        .caller_thread = k_current_get(),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        .shm_handle = in_shm_handle,
#endif
    };

    int ret = ipc_put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        ipc_pending_table_release(service, entry);
        ipc_service_pending_unlock(service);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (in_shm_handle != 0) {
            ipc_shm_release(service, in_shm_handle);
        }
#endif
        return ret;
    }

    /* 仅在请求成功入队后才写出 ID，避免失败路径污染调用方的 request_id。 */
    if (out_request_id != NULL) {
        *out_request_id = request_id;
    }

    return 0;
}

int ipc_call_future(ipc_service_t* service, const void* data, size_t data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                    ipc_shm_handle_t in_shm_handle,
#endif
                    ipc_future_t** out_future) {
    if (out_future == NULL) {
        return -EINVAL;
    }

    /* 输出参数在任何错误返回前都置为确定值。 */
    *out_future = NULL;

    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    if (in_shm_handle != 0 && !ipc_shm_is_valid(service, in_shm_handle)) {
        return -EINVAL;
    }
#endif

    ipc_request_id_t request_id = ipc_generate_request_id();

    ipc_service_pending_lock(service);

    ipc_future_t* future = ipc_pending_table_alloc_future(service);

    if (future == NULL) {
        ipc_service_pending_unlock(service);
        return -ENOMEM;
    }

    future->request_id = request_id;
    atomic_set(&future->completed, 0);
    future->result = 0;
    future->data = NULL;
    future->data_size = 0;
    k_sem_reset(&future->semaphore);

    ipc_pending_request_t* entry = ipc_pending_table_alloc(service);

    if (entry == NULL) {
        ipc_pending_table_release_future(service, future);
        ipc_service_pending_unlock(service);
        return -ENOMEM;
    }

    ipc_pending_table_init_entry(service, entry, request_id, k_current_get(), NULL, NULL, future);

    ipc_service_pending_unlock(service);

    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = NULL,
        .callback_user_data = NULL,
        .caller_thread = k_current_get(),
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        .shm_handle = in_shm_handle,
#endif
    };

    int ret = ipc_put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        ipc_pending_table_release(service, entry);
        ipc_pending_table_release_future(service, future);
        ipc_service_pending_unlock(service);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (in_shm_handle != 0) {
            ipc_shm_release(service, in_shm_handle);
        }
#endif
        return ret;
    }

    *out_future = future;

    return 0;
}

int ipc_future_wait(ipc_service_t* service, ipc_future_t* future, int* out_result, const void** out_data,
                    size_t* out_data_size, k_timeout_t timeout) {
    if (future == NULL) {
        return -EINVAL;
    }

    if (service == NULL) {
        return -EINVAL;
    }

    if (!ipc_pending_table_future_belongs(service, future)) {
        return -EINVAL;
    }

    if (atomic_get(&future->completed) == 0) {
        int ret = k_sem_take(&future->semaphore, timeout);

        if (ret != 0) {
            return ret;
        }

        k_sem_give(&future->semaphore);
    }

    if (out_result != NULL) {
        *out_result = future->result;
    }

    if (out_data != NULL) {
        *out_data = future->data;
    }

    if (out_data_size != NULL) {
        *out_data_size = future->data_size;
    }

    return 0;
}

bool ipc_future_is_ready(ipc_future_t* future) {
    if (future == NULL) {
        return false;
    }

    return atomic_get(&future->completed) != 0;
}

int ipc_future_release(ipc_service_t* service, ipc_future_t* future) {
    if (service == NULL || future == NULL) {
        return -EINVAL;
    }

    if (!ipc_pending_table_future_belongs(service, future)) {
        return -EINVAL;
    }

    ipc_service_pending_lock(service);

    if (ipc_pending_table_future_in_free_list(service, future)) {
        ipc_service_pending_unlock(service);
        return -EALREADY;
    }

    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use && service->pending_requests[i].future == future) {
            ipc_service_pending_unlock(service);
            return -EBUSY;
        }
    }

    ipc_pending_table_release_future(service, future);
    ipc_service_pending_unlock(service);

    return 0;
}

int ipc_service_cancel(ipc_service_t* service, ipc_request_id_t request_id) {
    if (service == NULL) {
        return -EINVAL;
    }

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = ipc_pending_table_find(service, request_id);
    int                    ret = -ENOENT;

    if (entry != NULL) {
        if (entry->completed) {
            ipc_service_pending_unlock(service);
            return -EALREADY;
        }

        if (entry->future != NULL) {
            entry->future->result = -ECANCELED;
            entry->future->data = NULL;
            entry->future->data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (entry->shm_handle != 0) {
                ipc_shm_release(service, entry->shm_handle);
                entry->shm_handle = 0;
            }
#endif
            atomic_set(&entry->future->completed, 1);
            k_sem_give(&entry->future->semaphore);
            entry->future = NULL;
            ipc_pending_table_release(service, entry);
            ret = 0;
        } else if (entry->callback != NULL) {
            ipc_pending_table_release(service, entry);
            ret = 0;
        } else {
            entry->canceled = true;
            entry->result = -ECANCELED;
            entry->response_data = NULL;
            entry->response_data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            ipc_pending_table_release_shm(service, entry);
#endif
            k_sem_give(&entry->response_sem);
            ret = 0;
        }
    }

    ipc_service_pending_unlock(service);

    return ret;
}
