/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ipc_service.c
 * @brief 基于专用线程 + 双消息队列的 IPC 服务实现
 *
 * 架构说明：
 * - 工作线程（worker）：从请求队列取消息，调用用户 service_func 处理
 * - 分发线程（dispatcher）：从响应队列取消息，根据 request_id 查找 pending 表投递响应
 * - 请求队列：调用者 -> 工作线程
 * - 响应队列：工作线程 -> 分发线程 -> 调用者
 *
 * 设计要点：
 * - 无堆分配：所有内存静态分配
 * - pending 表与 future 空闲链表受 pending_lock 保护
 * - 停止时向两队列投递哑包以唤醒阻塞的 k_msgq_get
 *
 * 三种调用模式：
 * - SYNC：使用 response_sem 信号量同步
 * - ASYNC：直接在分发线程中调用回调
 * - FUTURE：设置 future 完成状态并 signal 信号量
 */

#include "ipc_service.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(thread_ipc_svc, CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL);

/**
 * @brief 全局单调递增请求 ID 计数器
 * @note 跳过 0，避免与未初始化状态混淆
 */
static atomic_t s_request_id_counter;

/**
 * @brief 生成唯一的请求 ID
 *
 * @return 非零的请求 ID
 */
ipc_request_id_t ipc_generate_request_id(void) {
    ipc_request_id_t id;

    do {
        id = (ipc_request_id_t) atomic_inc(&s_request_id_counter);
    } while (id == 0U);
    return id;
}

/**
 * @brief 按 request_id 查找待处理请求条目
 *
 * @param service 服务实例
 * @param request_id 请求 ID
 * @return 指向待处理条目的指针，未找到返回 NULL
 *
 * @note 调用前必须持有 pending_lock
 */
static ipc_pending_request_t* find_pending_entry(ipc_service_t* service, ipc_request_id_t request_id) {
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use && service->pending_requests[i].request_id == request_id) {
            return &service->pending_requests[i];
        }
    }
    return NULL;
}

/**
 * @brief 分配待处理请求条目
 *
 * @param service 服务实例
 * @return 指向空闲条目的指针，无空闲返回 NULL
 *
 * @note 调用前必须持有 pending_lock
 */
static ipc_pending_request_t* allocate_pending_entry(ipc_service_t* service) {
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (!service->pending_requests[i].in_use) {
            return &service->pending_requests[i];
        }
    }
    return NULL;
}

/**
 * @brief 初始化待处理请求条目
 *
 * @param service 服务实例
 * @param entry 待初始化的条目
 * @param request_id 请求 ID
 * @param caller_thread 调用者线程
 * @param callback 异步回调函数（可为 NULL）
 * @param callback_user_data 回调用户数据
 * @param future 关联的 future（可为 NULL）
 *
 * @note 调用前必须持有 pending_lock
 */
static void init_pending_entry(ipc_service_t* service, ipc_pending_request_t* entry, ipc_request_id_t request_id,
                               struct k_thread* caller_thread, ipc_async_callback_t callback, void* callback_user_data,
                               ipc_future_t* future) {
    ARG_UNUSED(service);
    entry->request_id = request_id;
    entry->caller_thread = caller_thread;
    entry->callback = callback;
    entry->callback_user_data = callback_user_data;
    entry->future = future;
    entry->result = 0;
    entry->response_data = NULL;
    entry->response_data_size = 0;
    entry->in_use = true;
    k_sem_init(&entry->response_sem, 0, 1);
}

/**
 * @brief 释放待处理请求条目
 *
 * @param entry 待释放的条目
 *
 * @note 调用前必须持有 pending_lock
 */
static void release_pending_entry(ipc_pending_request_t* entry) {
    entry->in_use = false;
    entry->callback = NULL;
    entry->future = NULL;
}

/**
 * @brief 分配 future 对象
 *
 * @param service 服务实例
 * @return 指向 future 的指针，无空闲返回 NULL
 *
 * @note 从空闲链表头部取对象
 * @note 调用前必须持有 pending_lock
 */
static ipc_future_t* allocate_future(ipc_service_t* service) {
    if (service->free_futures == NULL) {
        return NULL;
    }

    ipc_future_t* future = service->free_futures;

    service->free_futures = future->next;
    future->next = NULL;

    return future;
}

/**
 * @brief 释放 future 对象回空闲链表
 *
 * @param service 服务实例
 * @param future 待释放的 future
 *
 * @note 重置 future 状态并插入链表头部
 * @note 调用前必须持有 pending_lock
 */
static void release_future(ipc_service_t* service, ipc_future_t* future) {
    future->request_id = 0U;
    atomic_set(&future->completed, 0);
    future->result = 0;
    future->data = NULL;
    future->data_size = 0;
    future->next = service->free_futures;
    service->free_futures = future;
}

/**
 * @brief 检查 future 是否属于指定服务
 *
 * @param service 服务实例
 * @param future future 对象
 * @return true 属于此服务，false 不属于
 */
static bool future_belongs_to_service(const ipc_service_t* service, const ipc_future_t* future) {
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (future == &service->futures[i]) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 检查 future 是否已在空闲链表中
 *
 * @note 调用前必须持有 pending_lock
 */
static bool future_is_in_free_list(const ipc_service_t* service, const ipc_future_t* future) {
    const ipc_future_t* it = service->free_futures;

    while (it != NULL) {
        if (it == future) {
            return true;
        }
        it = it->next;
    }

    return false;
}

/**
 * @brief 工作线程函数：处理请求
 *
 * 工作流程：
 * 1. 从请求队列获取消息（阻塞）
 * 2. 检查 shutdown 标志，如是则退出
 * 3. 调用用户 service_func 处理请求
 * 4. 将结果写入响应队列
 *
 * @param p1 服务实例指针
 * @param p2 未使用
 * @param p3 未使用
 */
static void service_thread_func(void* p1, void* p2, void* p3) {
    ipc_service_t*     service = (ipc_service_t*) p1;
    ipc_request_msg_t  request_msg;
    ipc_response_msg_t response_msg;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("IPC service '%s' worker started", service->name);

    for (;;) {
        /* 阻塞等待请求，shutdown 时通过哑消息唤醒 */
        int ret = k_msgq_get(&service->request_queue, &request_msg, K_FOREVER);

        if (ret != 0) {
            continue;
        }

        /* shutdown 后先收到哑包再退出，避免在已置位时仍调用 service_func */
        if (service->shutdown) {
            break;
        }

        void*  out_data = NULL;
        size_t out_data_size = 0;

        LOG_DBG("Processing request %u", request_msg.request_id);

        /* 调用用户服务函数处理请求 */
        int result = service->service_func(request_msg.request_id, request_msg.data, request_msg.data_size, &out_data,
                                           &out_data_size);

        /* 构建响应消息 */
        response_msg.request_id = request_msg.request_id;
        response_msg.result = result;
        response_msg.data = out_data;
        response_msg.data_size = out_data_size;
        response_msg.caller_thread = request_msg.caller_thread;

        /* 发送响应到响应队列 */
        ret = k_msgq_put(&service->response_queue, &response_msg, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("Failed to send response for request %u", request_msg.request_id);
        }
    }

    LOG_INF("IPC service '%s' worker stopped", service->name);
}

/**
 * @brief 响应分发线程函数：投递响应到调用者
 *
 * 工作流程：
 * 1. 从响应队列获取消息（阻塞）
 * 2. 检查 shutdown 标志，如是则退出
 * 3. 根据 request_id 查找 pending 条目
 * 4. 根据调用模式投递响应：
 *    - ASYNC：复制参数后在锁外调用回调
 *    - FUTURE：设置 future 状态并 signal 信号量
 *    - SYNC：signal 响应信号量
 *
 * @param p1 服务实例指针
 * @param p2 未使用
 * @param p3 未使用
 */
static void response_dispatcher_thread(void* p1, void* p2, void* p3) {
    ipc_service_t*     service = (ipc_service_t*) p1;
    ipc_response_msg_t response_msg;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("IPC service '%s' dispatcher started", service->name);

    for (;;) {
        /* 阻塞等待响应，shutdown 时通过哑消息唤醒 */
        int ret = k_msgq_get(&service->response_queue, &response_msg, K_FOREVER);

        if (ret != 0) {
            continue;
        }

        if (service->shutdown) {
            break;
        }

        k_mutex_lock(&service->pending_lock, K_FOREVER);

        /* 根据 request_id 查找对应的待处理请求 */
        ipc_pending_request_t* entry = find_pending_entry(service, response_msg.request_id);

        if (entry != NULL) {
            /* 保存响应数据到 pending 条目 */
            entry->result = response_msg.result;
            entry->response_data = response_msg.data;
            entry->response_data_size = response_msg.data_size;

            /* ASYNC 模式：有回调函数 */
            if (entry->callback != NULL) {
                /* 先复制回调参数并释放槽位，再在锁外调用用户回调，避免死锁 */
                ipc_async_callback_t cb = entry->callback;
                void*                ud = entry->callback_user_data;
                ipc_request_id_t     rid = entry->request_id;
                int                  res = entry->result;
                const void*          rdata = entry->response_data;
                size_t               rsize = entry->response_data_size;

                release_pending_entry(entry);
                k_mutex_unlock(&service->pending_lock);

                /* 在锁外调用回调 */
                cb(rid, res, rdata, rsize, ud);
                continue;
            }

            /* FUTURE 模式：有关联的 future */
            if (entry->future != NULL) {
                entry->future->result = entry->result;
                entry->future->data = entry->response_data;
                entry->future->data_size = entry->response_data_size;
                atomic_set(&entry->future->completed, 1);
                k_sem_give(&entry->future->semaphore);
                entry->future = NULL;
                release_pending_entry(entry);
            } else {
                /* SYNC 模式：signal 响应信号量唤醒等待线程 */
                k_sem_give(&entry->response_sem);
            }
        } else {
            LOG_WRN("No pending entry for response %u", response_msg.request_id);
        }

        k_mutex_unlock(&service->pending_lock);
    }

    LOG_INF("IPC service '%s' dispatcher stopped", service->name);
}

/* =============================================================================
 * 核心 API 实现 (Core API Implementation)
 * ============================================================================= */

/**
 * @brief 初始化 IPC 服务实例
 */
int ipc_service_init(ipc_service_t* service, const char* name, ipc_service_func_t service_func, size_t stack_size,
                     int priority, size_t request_queue_size, size_t response_queue_size) {
    if (service == NULL || name == NULL || service_func == NULL) {
        return -EINVAL;
    }

    /* 验证配置参数必须与 Kconfig 一致（静态分配要求） */
    if (stack_size != (size_t) CONFIG_THREAD_IPC_SERVICE_STACK_SIZE ||
        request_queue_size != (size_t) CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE ||
        response_queue_size != (size_t) CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE) {
        return -EINVAL;
    }

    memset(service, 0, sizeof(ipc_service_t));

    service->name = name;
    service->service_func = service_func;
    service->priority = priority;
    service->running = false;
    service->shutdown = false;
    k_mutex_init(&service->state_lock);

    /* 初始化请求队列 */
    k_msgq_init(&service->request_queue, (char*) service->request_queue_buf, sizeof(ipc_request_msg_t),
                CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE);

    /* 初始化响应队列 */
    k_msgq_init(&service->response_queue, (char*) service->response_queue_buf, sizeof(ipc_response_msg_t),
                CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE);

    /* 初始化 pending 表锁 */
    k_mutex_init(&service->pending_lock);

    /* 初始化 pending 表所有条目为未使用 */
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        service->pending_requests[i].in_use = false;
    }

    /* 初始化 future 对象池（链表） */
    service->free_futures = NULL;
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        service->futures[i].request_id = 0;
        atomic_set(&service->futures[i].completed, 0);
        service->futures[i].next = service->free_futures;
        k_sem_init(&service->futures[i].semaphore, 0, 1);
        service->free_futures = &service->futures[i];
    }

    LOG_INF("IPC service '%s' initialized", name);

    return 0;
}

/**
 * @brief 启动 IPC 服务
 */
int ipc_service_start(ipc_service_t* service) {
    if (service == NULL || !service->service_func) {
        return -EINVAL;
    }

    k_mutex_lock(&service->state_lock, K_FOREVER);

    if (service->running) {
        k_mutex_unlock(&service->state_lock);
        return -EALREADY;
    }

    service->shutdown = false;

    /* 创建工作线程 */
    k_thread_create(&service->thread, service->worker_stack, K_KERNEL_STACK_SIZEOF(service->worker_stack),
                    service_thread_func, service, NULL, NULL, service->priority, 0, K_NO_WAIT);
#if IS_ENABLED(CONFIG_THREAD_NAME)
    k_thread_name_set(&service->thread, service->name);
#endif

    /* 创建响应分发线程 */
    k_thread_create(&service->response_thread, service->dispatcher_stack,
                    K_KERNEL_STACK_SIZEOF(service->dispatcher_stack), response_dispatcher_thread, service, NULL, NULL,
                    service->priority, 0, K_NO_WAIT);
#if IS_ENABLED(CONFIG_THREAD_NAME)
    k_thread_name_set(&service->response_thread, "ipc_disp");
#endif

    service->running = true;
    k_mutex_unlock(&service->state_lock);

    LOG_INF("IPC service '%s' started", service->name);

    return 0;
}

/**
 * @brief 停止 IPC 服务
 */
int ipc_service_stop(ipc_service_t* service) {
    if (service == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&service->state_lock, K_FOREVER);
    if (!service->running) {
        k_mutex_unlock(&service->state_lock);
        return 0;
    }
    service->shutdown = true;
    k_mutex_unlock(&service->state_lock);

    /* 非阻塞投递哑消息，唤醒可能阻塞在 k_msgq_get 的 worker/dispatcher */
    ipc_request_msg_t dummy_request;

    memset(&dummy_request, 0, sizeof(dummy_request));
    (void) k_msgq_put(&service->request_queue, &dummy_request, K_NO_WAIT);

    ipc_response_msg_t dummy_response;

    memset(&dummy_response, 0, sizeof(dummy_response));
    (void) k_msgq_put(&service->response_queue, &dummy_response, K_NO_WAIT);

    /* 等待两线程退出 */
    k_thread_join(&service->thread, K_FOREVER);
    k_thread_join(&service->response_thread, K_FOREVER);

    k_mutex_lock(&service->state_lock, K_FOREVER);
    service->running = false;
    k_mutex_unlock(&service->state_lock);

    LOG_INF("IPC service '%s' stopped", service->name);

    return 0;
}

/* =============================================================================
 * 调用 API 实现 (Call API Implementation)
 * ============================================================================= */

/**
 * @brief 同步调用 IPC 服务
 */
int ipc_call_sync(ipc_service_t* service, const void* data, size_t data_size, void** out_data, size_t* out_data_size,
                  k_timeout_t timeout) {
    if (service == NULL || !service->running) {
        return -EINVAL;
    }

    if (data == NULL || out_data == NULL || out_data_size == NULL) {
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    /* 分配 pending 条目 */
    ipc_pending_request_t* entry = allocate_pending_entry(service);

    if (entry == NULL) {
        k_mutex_unlock(&service->pending_lock);
        return -ENOMEM;
    }

    /* 初始化为 SYNC 模式（callback=NULL, future=NULL） */
    init_pending_entry(service, entry, request_id, k_current_get(), NULL, NULL, NULL);

    k_mutex_unlock(&service->pending_lock);

    /* 构建请求消息 */
    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = NULL,
        .callback_user_data = NULL,
        .caller_thread = k_current_get(),
    };

    /* 发送请求到请求队列 */
    int ret = k_msgq_put(&service->request_queue, &request_msg, K_FOREVER);

    if (ret != 0) {
        k_mutex_lock(&service->pending_lock, K_FOREVER);
        release_pending_entry(entry);
        k_mutex_unlock(&service->pending_lock);
        return ret;
    }

    /* 等待响应信号量 */
    ret = k_sem_take(&entry->response_sem, timeout);
    if (ret != 0) {
        k_mutex_lock(&service->pending_lock, K_FOREVER);
        release_pending_entry(entry);
        k_mutex_unlock(&service->pending_lock);
        return ret;
    }

    /* 复制输出数据 */
    *out_data = (void*) entry->response_data;
    *out_data_size = entry->response_data_size;
    int result = entry->result;

    /* 释放 pending 条目 */
    k_mutex_lock(&service->pending_lock, K_FOREVER);
    release_pending_entry(entry);
    k_mutex_unlock(&service->pending_lock);

    return result;
}

/**
 * @brief 异步调用 IPC 服务
 */
int ipc_call_async(ipc_service_t* service, const void* data, size_t data_size, ipc_async_callback_t callback,
                   void* user_data, ipc_request_id_t* out_request_id) {
    if (service == NULL || !service->running) {
        return -EINVAL;
    }

    if (data == NULL || callback == NULL) {
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    if (out_request_id != NULL) {
        *out_request_id = request_id;
    }

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    ipc_pending_request_t* entry = allocate_pending_entry(service);

    if (entry == NULL) {
        k_mutex_unlock(&service->pending_lock);
        return -ENOMEM;
    }

    /* 初始化为 ASYNC 模式（设置 callback） */
    init_pending_entry(service, entry, request_id, k_current_get(), callback, user_data, NULL);

    k_mutex_unlock(&service->pending_lock);

    /* 构建请求消息 */
    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = callback,
        .callback_user_data = user_data,
        .caller_thread = k_current_get(),
    };

    /* 发送请求到请求队列 */
    int ret = k_msgq_put(&service->request_queue, &request_msg, K_FOREVER);

    if (ret != 0) {
        k_mutex_lock(&service->pending_lock, K_FOREVER);
        release_pending_entry(entry);
        k_mutex_unlock(&service->pending_lock);
        return ret;
    }

    /* 立即返回，回调将在分发线程中执行 */
    return 0;
}

/**
 * @brief Future 模式调用 IPC 服务
 */
int ipc_call_future(ipc_service_t* service, const void* data, size_t data_size, ipc_future_t** out_future) {
    if (service == NULL || !service->running) {
        return -EINVAL;
    }

    if (data == NULL || out_future == NULL) {
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    /* 分配 future 对象 */
    ipc_future_t* future = allocate_future(service);

    if (future == NULL) {
        k_mutex_unlock(&service->pending_lock);
        return -ENOMEM;
    }

    /* 初始化 future 状态 */
    future->request_id = request_id;
    atomic_set(&future->completed, 0);
    future->result = 0;
    future->data = NULL;
    future->data_size = 0;
    k_sem_reset(&future->semaphore);

    /* 分配 pending 条目 */
    ipc_pending_request_t* entry = allocate_pending_entry(service);

    if (entry == NULL) {
        release_future(service, future);
        k_mutex_unlock(&service->pending_lock);
        return -ENOMEM;
    }

    /* 初始化为 FUTURE 模式（设置 future） */
    init_pending_entry(service, entry, request_id, k_current_get(), NULL, NULL, future);

    k_mutex_unlock(&service->pending_lock);

    /* 构建请求消息 */
    ipc_request_msg_t request_msg = {
        .request_id = request_id,
        .data = data,
        .data_size = data_size,
        .callback = NULL,
        .callback_user_data = NULL,
        .caller_thread = k_current_get(),
    };

    /* 发送请求到请求队列 */
    int ret = k_msgq_put(&service->request_queue, &request_msg, K_FOREVER);

    if (ret != 0) {
        k_mutex_lock(&service->pending_lock, K_FOREVER);
        release_pending_entry(entry);
        release_future(service, future);
        k_mutex_unlock(&service->pending_lock);
        return ret;
    }

    *out_future = future;

    return 0;
}

/* =============================================================================
 * Future API 实现 (Future API Implementation)
 * ============================================================================= */

/**
 * @brief 等待 Future 结果
 */
int ipc_future_wait(ipc_future_t* future, int* out_result, const void** out_data, size_t* out_data_size,
                    k_timeout_t timeout) {
    if (future == NULL) {
        return -EINVAL;
    }

    /* 等待 future 信号量 */
    int ret = k_sem_take(&future->semaphore, timeout);

    if (ret != 0) {
        return ret;
    }

    /* 复制结果数据 */
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

/**
 * @brief 检查 Future 是否就绪
 */
bool ipc_future_is_ready(ipc_future_t* future) {
    if (future == NULL) {
        return false;
    }

    return atomic_get(&future->completed) != 0;
}

/**
 * @brief 释放 Future 对象
 */
int ipc_future_release(ipc_service_t* service, ipc_future_t* future) {
    if (service == NULL || future == NULL) {
        return -EINVAL;
    }

    /* 验证 future 属于此服务 */
    if (!future_belongs_to_service(service, future)) {
        return -EINVAL;
    }

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    /* 防止重复释放（重复入空闲链表） */
    if (future_is_in_free_list(service, future)) {
        k_mutex_unlock(&service->pending_lock);
        return -EALREADY;
    }

    /* 仍被 pending 请求引用时不可释放 */
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use && service->pending_requests[i].future == future) {
            k_mutex_unlock(&service->pending_lock);
            return -EBUSY;
        }
    }

    release_future(service, future);
    k_mutex_unlock(&service->pending_lock);

    return 0;
}

/**
 * @brief 获取待处理请求数量
 */
size_t ipc_service_get_pending_count(ipc_service_t* service) {
    if (service == NULL) {
        return 0;
    }

    size_t count = 0;

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    /* 统计 in_use 为 true 的条目数 */
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use) {
            count++;
        }
    }

    k_mutex_unlock(&service->pending_lock);

    return count;
}

/**
 * @brief 取消待处理请求
 */
int ipc_service_cancel(ipc_service_t* service, ipc_request_id_t request_id) {
    if (service == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&service->pending_lock, K_FOREVER);

    ipc_pending_request_t* entry = find_pending_entry(service, request_id);
    int                    ret = -ENOENT;

    if (entry != NULL) {
        /* 根据调用模式采取不同的取消策略 */
        if (entry->future != NULL) {
            /* FUTURE 模式：设置取消状态并唤醒等待者 */
            entry->future->result = -ECANCELED;
            entry->future->data = NULL;
            entry->future->data_size = 0;
            atomic_set(&entry->future->completed, 1);
            k_sem_give(&entry->future->semaphore);
            entry->future = NULL;
            release_pending_entry(entry);
            ret = 0;
        } else if (entry->callback != NULL) {
            /* ASYNC 模式：直接释放条目（回调不会被调用） */
            release_pending_entry(entry);
            ret = 0;
        } else {
            /* SYNC 模式：设置取消状态并唤醒等待线程 */
            entry->result = -ECANCELED;
            entry->response_data = NULL;
            entry->response_data_size = 0;
            k_sem_give(&entry->response_sem);
            ret = 0;
        }
    }

    k_mutex_unlock(&service->pending_lock);

    return ret;
}
