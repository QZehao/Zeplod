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
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 * 2026-05-09       1.1            zeh            stop：join 超时后 abort 并重试
 * join；worker：取消后跳过请求；dispatcher：无 pending 降级日志
 *
 */

#include "ipc_service.h"

#include "lock_order.h"
#include "state_machine.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(thread_ipc_svc, CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL);

/* =============================================================================
 * SIL-2: 配置验证宏
 * ============================================================================= */

/** 最大合理的请求队列大小 */
#ifndef IPC_SERVICE_MAX_QUEUE_SIZE
#define IPC_SERVICE_MAX_QUEUE_SIZE 1024U
#endif

/** 最大合理的线程栈大小 */
#ifndef IPC_SERVICE_MIN_STACK_SIZE
#define IPC_SERVICE_MIN_STACK_SIZE 512U
#endif

/** 线程 join 超时时间（毫秒） */
#ifndef IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS
#define IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS 500U
#endif

/** 线程循环中 msgq 获取的超时时间（毫秒） */
#ifndef IPC_SERVICE_MSGQ_TIMEOUT_MS
#define IPC_SERVICE_MSGQ_TIMEOUT_MS 100U
#endif

/**
 * @brief 判断 k_timeout_t 是否为「零等待」（兼容不同 Zephyr 版本的 k_timeout_t 布局）
 */
static inline bool ipc_timeout_is_zero(k_timeout_t timeout) {
    return K_TIMEOUT_EQ(timeout, K_NO_WAIT);
}

/**
 * @brief 全局单调递增请求 ID 计数器
 * @note 跳过 0，避免与未初始化状态混淆
 */
static atomic_t s_request_id_counter = ATOMIC_INIT(1);

BUILD_ASSERT(CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE >= 2);
BUILD_ASSERT(CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE <= IPC_SERVICE_MAX_QUEUE_SIZE);
BUILD_ASSERT(CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE >= 2);
BUILD_ASSERT(CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE <= IPC_SERVICE_MAX_QUEUE_SIZE);
BUILD_ASSERT(CONFIG_THREAD_IPC_SERVICE_STACK_SIZE >= IPC_SERVICE_MIN_STACK_SIZE);

/**
 * @brief 生成唯一的请求 ID
 *
 * @return 非零的请求 ID
 *
 * @note SIL-2: 使用原子递增操作，避免竞态条件
 * @note 计数器达到 UINT32_MAX 后自动回绕，跳过 0 值
 */
ipc_request_id_t ipc_generate_request_id(void) {
    ipc_request_id_t id;

    do {
        id = (ipc_request_id_t) atomic_inc(&s_request_id_counter);
        /* SIL-2: 跳过 0 值（跳过一次意味着需要再次递增） */
        if (id == 0U) {
            id = (ipc_request_id_t) atomic_inc(&s_request_id_counter);
        }
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
    entry->completed = false;
    entry->canceled = false;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    entry->shm_handle = 0;
#endif
    k_sem_init(&entry->response_sem, 0, 1);
}

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
/**
 * @brief 释放 pending 槽位中暂存的共享内存引用
 *
 * @note 调用前必须持有 pending_lock
 */
static void release_pending_shm_handle(ipc_service_t* service, ipc_pending_request_t* entry) {
    if (entry->shm_handle != 0) {
        LOG_DBG("release_pending_entry: releasing shm_handle %u", entry->shm_handle);
        ipc_shm_release(service, entry->shm_handle);
        entry->shm_handle = 0;
    }
}
#endif

/**
 * @brief 释放待处理请求条目
 *
 * @param entry 待释放的条目
 *
 * @note 调用前必须持有 pending_lock
 * @note SIL-2: 自动释放共享内存引用
 */
static void release_pending_entry(ipc_service_t* service, ipc_pending_request_t* entry) {
    ARG_UNUSED(service);
    entry->in_use = false;
    entry->completed = false;
    entry->canceled = false;
    entry->callback = NULL;
    entry->future = NULL;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    /* SIL-2: 释放共享内存引用（如果有） */
    release_pending_shm_handle(service, entry);
#endif
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
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    /* SIL-2: 释放 future 中的共享内存引用 */
    if (future->shm_handle != 0) {
        LOG_DBG("release_future: releasing shm_handle %u", future->shm_handle);
        ipc_shm_release(service, future->shm_handle);
        future->shm_handle = 0;
    }
#endif
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
 * @brief 判断服务是否可以接收新请求
 */
static bool ipc_service_is_accepting_requests(ipc_service_t* service) {
    if (service == NULL || !service->initialized) {
        return false;
    }

    ipc_service_state_lock(service);
    bool accepting = service->running && atomic_get(&service->shutdown) == 0;
    ipc_service_state_unlock(service);

    return accepting;
}

static zepl_state_t ipc_service_lifecycle_state_locked(const ipc_service_t* service) {
    if (service == NULL) {
        return ZEP_STATE_ERROR;
    }

    return zepl_state_machine_get(&service->lifecycle);
}

static void ipc_service_state_lock(ipc_service_t* service) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_STATE, (uintptr_t) &service->state_lock);
    k_mutex_lock(&service->state_lock, K_FOREVER);
}

static void ipc_service_state_unlock(ipc_service_t* service) {
    k_mutex_unlock(&service->state_lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_STATE, (uintptr_t) &service->state_lock);
}

static void ipc_service_pending_lock(ipc_service_t* service) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_TABLE, (uintptr_t) &service->pending_lock);
    k_mutex_lock(&service->pending_lock, K_FOREVER);
}

static void ipc_service_pending_unlock(ipc_service_t* service) {
    k_mutex_unlock(&service->pending_lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_TABLE, (uintptr_t) &service->pending_lock);
}

/**
 * @brief 带停止感知的消息投递，避免队列满时永久阻塞 stop 流程
 */
static int put_msgq_until_shutdown(struct k_msgq* queue, const void* msg, const atomic_t* shutdown) {
    while (atomic_get(shutdown) == 0) {
        int ret = k_msgq_put(queue, msg, K_MSEC(IPC_SERVICE_MSGQ_TIMEOUT_MS));

        if (ret == 0) {
            return 0;
        }

        if (ret != -EAGAIN) {
            return ret;
        }
    }

    return -ECANCELED;
}

/**
 * @brief 清空残留队列，并释放队列消息携带的共享内存引用
 */
static void drain_queued_messages(ipc_service_t* service) {
    ipc_request_msg_t request_msg;
    while (k_msgq_get(&service->request_queue, &request_msg, K_NO_WAIT) == 0) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (request_msg.shm_handle != 0) {
            ipc_shm_release(service, request_msg.shm_handle);
        }
#endif
    }

    ipc_response_msg_t response_msg;
    while (k_msgq_get(&service->response_queue, &response_msg, K_NO_WAIT) == 0) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (response_msg.shm_handle != 0) {
            ipc_shm_release(service, response_msg.shm_handle);
        }
#endif
    }
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

    LOG_DBG("IPC service '%s' worker started", service->name);

    for (;;) {
        /* SIL-2: 使用有限超时而非永久阻塞，确保能响应 shutdown */
        int ret = k_msgq_get(&service->request_queue, &request_msg, K_MSEC(IPC_SERVICE_MSGQ_TIMEOUT_MS));

        if (ret != 0) {
            if (atomic_get(&service->shutdown) != 0) {
                LOG_DBG("Worker thread exiting on shutdown signal");
                break;
            }
            continue;
        }

        if (atomic_get(&service->shutdown) != 0) {
            LOG_DBG("Worker thread detected shutdown after receiving request");
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (request_msg.shm_handle != 0) {
                ipc_shm_release(service, request_msg.shm_handle);
            }
#endif
            break;
        }

        /* SIL-2: 检查是否为哑消息（用于唤醒线程），request_id=0 表示无效请求 */
        if (request_msg.request_id == 0U) {
            LOG_DBG("Received dummy request, ignoring");
            continue;
        }

        /* SIL-2: 验证 service_func 非空 */
        if (service->service_func == NULL) {
            LOG_ERR("Service function is NULL, dropping request %u", request_msg.request_id);
            /* SIL-2: 如果有共享内存句柄，释放引用 */
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (request_msg.shm_handle != 0) {
                ipc_shm_release(service, request_msg.shm_handle);
            }
#endif
            continue;
        }

        /* 取消/超时已释放 pending 后，队列里仍可能有该 request_id：跳过业务并释放随请求传递的 shm 引用 */
        ipc_service_pending_lock(service);
        ipc_pending_request_t* tracked = find_pending_entry(service, request_msg.request_id);
        bool                   still_tracked = (tracked != NULL && tracked->in_use && !tracked->canceled);
        ipc_service_pending_unlock(service);

        if (!still_tracked) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (request_msg.shm_handle != 0) {
                ipc_shm_release(service, request_msg.shm_handle);
            }
#endif
            LOG_DBG("Skipping request %u (pending slot released)", request_msg.request_id);
            continue;
        }

        void*  out_data = NULL;
        size_t out_data_size = 0;

        LOG_DBG("Processing request %u", request_msg.request_id);

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        bool request_shm_acquired = false;

        /* SIL-2: 如果请求带有共享内存，增加引用确保在处理期间有效 */
        if (request_msg.shm_handle != 0) {
            ipc_shm_acquire(service, request_msg.shm_handle);
            request_shm_acquired = true;
            LOG_DBG("Request %u: acquired shm handle %u", request_msg.request_id, request_msg.shm_handle);
        }
#endif

        /* 调用用户服务函数处理请求 */
        int result = service->service_func(request_msg.request_id, request_msg.data, request_msg.data_size, &out_data,
                                           &out_data_size);

        /* 构建响应消息 */
        response_msg.request_id = request_msg.request_id;
        response_msg.result = result;
        response_msg.data = out_data;
        response_msg.data_size = out_data_size;
        response_msg.caller_thread = request_msg.caller_thread;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        /* 输入侧随请求传递的句柄；否则若 out_data 在池内则解析 service 新分配的块 */
        response_msg.shm_handle = request_msg.shm_handle;
        if (response_msg.shm_handle == 0 && out_data != NULL) {
            ipc_shm_handle_t out_handle = ipc_shm_lookup_handle_by_ptr(service, out_data);

            if (out_handle != IPC_SHM_HANDLE_INVALID) {
                response_msg.shm_handle = out_handle;
            }
        }
#endif

        /* 发送响应到响应队列 */
        int put_ret = put_msgq_until_shutdown(&service->response_queue, &response_msg, &service->shutdown);
        if (put_ret != 0) {
            LOG_ERR("Failed to send response for request %u: %d", request_msg.request_id, put_ret);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            /* SIL-2: 发送失败时释放共享内存引用 */
            if (response_msg.shm_handle != 0) {
                ipc_shm_release(service, response_msg.shm_handle);
            }
#endif
        }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (request_shm_acquired) {
            ipc_shm_release(service, request_msg.shm_handle);
        }
#endif
    }

    LOG_DBG("IPC service '%s' worker stopped", service->name);
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

    LOG_DBG("IPC service '%s' dispatcher started", service->name);

    for (;;) {
        /* SIL-2: 使用有限超时而非永久阻塞，确保能响应 shutdown */
        int ret = k_msgq_get(&service->response_queue, &response_msg, K_MSEC(IPC_SERVICE_MSGQ_TIMEOUT_MS));

        if (ret != 0) {
            if (atomic_get(&service->shutdown) != 0) {
                LOG_DBG("Dispatcher thread exiting on shutdown signal");
                break;
            }
            continue;
        }

        if (atomic_get(&service->shutdown) != 0) {
            LOG_DBG("Dispatcher thread detected shutdown");
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (response_msg.shm_handle != 0) {
                ipc_shm_release(service, response_msg.shm_handle);
            }
#endif
            break;
        }

        /* SIL-2: 检查是否为哑消息（用于唤醒线程），request_id=0 表示无效响应 */
        if (response_msg.request_id == 0U) {
            LOG_DBG("Received dummy response, ignoring");
            continue;
        }

        ipc_service_pending_lock(service);

        /* 根据 request_id 查找对应的待处理请求 */
        ipc_pending_request_t* entry = find_pending_entry(service, response_msg.request_id);

        if (entry != NULL) {
            if (entry->canceled) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                if (response_msg.shm_handle != 0) {
                    ipc_shm_release(service, response_msg.shm_handle);
                }
#endif
                ipc_service_pending_unlock(service);
                continue;
            }

            /* 保存响应数据到 pending 条目 */
            entry->result = response_msg.result;
            entry->response_data = response_msg.data;
            entry->response_data_size = response_msg.data_size;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            entry->shm_handle = response_msg.shm_handle;
            response_msg.shm_handle = 0;
            ipc_shm_handle_t shm_handle = entry->shm_handle;
#endif

            /* ASYNC 模式：有回调函数 */
            if (entry->callback != NULL) {
                /* SIL-2: 先复制回调参数并释放槽位，再在锁外调用用户回调，避免死锁 */
                ipc_async_callback_t cb = entry->callback;
                void*                ud = entry->callback_user_data;
                ipc_request_id_t     rid = entry->request_id;
                int                  res = entry->result;
                const void*          rdata = entry->response_data;
                size_t               rsize = entry->response_data_size;

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                entry->shm_handle = 0;
#endif
                release_pending_entry(service, entry);
                ipc_service_pending_unlock(service);

                /* SIL-2: 验证回调函数非空后再调用 */
                if (cb != NULL) {
                    cb(rid, res, rdata, rsize, ud);
                } else {
                    LOG_ERR("NULL callback detected for request %u", rid);
                }

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                /* SIL-2: 回调执行完毕后释放共享内存引用 */
                if (shm_handle != 0) {
                    LOG_DBG("ASYNC callback done, releasing shm handle %u", shm_handle);
                    ipc_shm_release(service, shm_handle);
                }
#endif
                continue;
            }

            /* FUTURE 模式：有关联的 future */
            if (entry->future != NULL) {
                entry->future->result = entry->result;
                entry->future->data = entry->response_data;
                entry->future->data_size = entry->response_data_size;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                entry->future->shm_handle = entry->shm_handle;
                entry->shm_handle = 0;
#endif
                atomic_set(&entry->future->completed, 1);
                k_sem_give(&entry->future->semaphore);
                entry->future = NULL;
                release_pending_entry(service, entry);
            } else {
                /* SYNC 模式：signal 响应信号量唤醒等待线程 */
                entry->completed = true;
                k_sem_give(&entry->response_sem);
            }
        } else {
            /* 常见于 cancel/stop 与响应竞态；worker 侧已尽量避免投递孤立响应 */
            LOG_DBG("No pending entry for response %u", response_msg.request_id);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            if (response_msg.shm_handle != 0) {
                LOG_DBG("Releasing orphan shm handle %u for response %u", response_msg.shm_handle,
                        response_msg.request_id);
                ipc_shm_release(service, response_msg.shm_handle);
            }
#endif
        }

        ipc_service_pending_unlock(service);
    }

    LOG_DBG("IPC service '%s' dispatcher stopped", service->name);
}

/* =============================================================================
 * 核心 API 实现 (Core API Implementation)
 * ============================================================================= */

/**
 * @brief 初始化 IPC 服务实例
 */
int ipc_service_init(ipc_service_t* service, const char* name, ipc_service_func_t service_func, int priority) {
    if (service == NULL || name == NULL || service_func == NULL) {
        return -EINVAL;
    }

    if (service->initialized && service->running) {
        return -EBUSY;
    }

    memset(service, 0, sizeof(ipc_service_t));
    zepl_state_machine_init(&service->lifecycle, ZEP_STATE_UNINIT);

    service->name = name;
    service->service_func = service_func;
    service->priority = priority;
    service->initialized = true;
    service->running = false;
    atomic_set(&service->shutdown, 0);
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

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    /* 初始化共享内存池 */
    int shm_ret = ipc_shm_init(service);
    if (shm_ret != 0) {
        LOG_ERR("Failed to init shared memory pool: %d", shm_ret);
        service->initialized = false;
        return shm_ret;
    }
#endif

    (void) zepl_state_machine_try_transition(&service->lifecycle, ZEP_STATE_INITED);

    LOG_DBG("IPC service '%s' initialized", name);

    return 0;
}

/**
 * @brief 启动 IPC 服务
 *
 * @note SIL-2: k_thread_create 无返回值，线程创建失败无法检测
 *              静态分配的栈和控制块使得失败概率极低
 */
int ipc_service_start(ipc_service_t* service) {
    if (service == NULL || !service->initialized || !service->service_func) {
        return -EINVAL;
    }

    ipc_service_state_lock(service);

    zepl_state_t state = ipc_service_lifecycle_state_locked(service);

    if (state == ZEP_STATE_RUNNING) {
        ipc_service_state_unlock(service);
        return -EALREADY;
    }

    if (state == ZEP_STATE_UNINIT || state == ZEP_STATE_ERROR || state == ZEP_STATE_STOPPING) {
        ipc_service_state_unlock(service);
        return -EINVAL;
    }

    if (zepl_state_machine_try_transition(&service->lifecycle, ZEP_STATE_STARTING) != 0) {
        ipc_service_state_unlock(service);
        return -EINVAL;
    }

    atomic_set(&service->shutdown, 0);

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
    (void) zepl_state_machine_try_transition(&service->lifecycle, ZEP_STATE_RUNNING);
    ipc_service_state_unlock(service);

    LOG_DBG("IPC service '%s' started", service->name);

    return 0;
}

/**
 * @brief 停止 IPC 服务
 *
 * @note SIL-2: 添加超时保护，避免永久阻塞
 */
int ipc_service_stop(ipc_service_t* service) {
    if (service == NULL) {
        return -EINVAL;
    }

    ipc_service_state_lock(service);
    zepl_state_t state = ipc_service_lifecycle_state_locked(service);

    if (state == ZEP_STATE_UNINIT) {
        ipc_service_state_unlock(service);
        return -EINVAL;
    }

    if (!service->running || state == ZEP_STATE_INITED || state == ZEP_STATE_STOPPED) {
        ipc_service_state_unlock(service);
        return 0;
    }

    if (state == ZEP_STATE_ERROR) {
        ipc_service_state_unlock(service);
        return -EINVAL;
    }

    if (k_current_get() == &service->thread || k_current_get() == &service->response_thread) {
        ipc_service_state_unlock(service);
        return -EDEADLK;
    }

    if (state != ZEP_STATE_STOPPING) {
        if (zepl_state_machine_try_transition(&service->lifecycle, ZEP_STATE_STOPPING) != 0) {
            ipc_service_state_unlock(service);
            return -EINVAL;
        }
    }

    atomic_set(&service->shutdown, 1);
    ipc_service_state_unlock(service);

    /* SIL-2: 非阻塞投递哑消息，唤醒可能阻塞在 k_msgq_get 的 worker/dispatcher */
    ipc_request_msg_t dummy_request;
    memset(&dummy_request, 0, sizeof(dummy_request));
    (void) k_msgq_put(&service->request_queue, &dummy_request, K_NO_WAIT);

    ipc_response_msg_t dummy_response;
    memset(&dummy_response, 0, sizeof(dummy_response));
    (void) k_msgq_put(&service->response_queue, &dummy_response, K_NO_WAIT);

    /* SIL-2: 使用有限超时等待线程退出；超时则 abort 后再 join，避免 running=false 时线程仍访问 service */
    int ret1 = k_thread_join(&service->thread, K_MSEC(IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS));
    if (ret1 != 0) {
        LOG_ERR("Worker thread join failed: %d, aborting", ret1);
        k_thread_abort(&service->thread);
        ret1 = k_thread_join(&service->thread, K_MSEC(IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS));
    }

    int ret2 = k_thread_join(&service->response_thread, K_MSEC(IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS));
    if (ret2 != 0) {
        LOG_ERR("Dispatcher thread join failed: %d, aborting", ret2);
        k_thread_abort(&service->response_thread);
        ret2 = k_thread_join(&service->response_thread, K_MSEC(IPC_SERVICE_THREAD_JOIN_TIMEOUT_MS));
    }

    int stop_err = 0;
    if (ret1 != 0 || ret2 != 0) {
        LOG_ERR("IPC service '%s': thread did not terminate after abort (worker_ret=%d, disp_ret=%d)", service->name,
                ret1, ret2);
        stop_err = -EIO;
    }

    if (stop_err != 0) {
        return stop_err;
    }

    /* SIL-2: 清理队列残留消息，防止 stop 后再 start 时消费旧消息 */
    drain_queued_messages(service);
    k_msgq_purge(&service->request_queue);
    k_msgq_purge(&service->response_queue);

    if (stop_err == 0) {
        ipc_service_state_lock(service);
        service->running = false;
        atomic_set(&service->shutdown, 0);
        (void) zepl_state_machine_try_transition(&service->lifecycle, ZEP_STATE_STOPPED);
        ipc_service_state_unlock(service);
    }

    /* SIL-2: 清理所有 pending 请求，防止资源泄漏 */
    ipc_service_pending_lock(service);
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        ipc_pending_request_t* entry = &service->pending_requests[i];

        if (!entry->in_use) {
            continue;
        }

        if (entry->future != NULL) {
            entry->future->result = -ECANCELED;
            entry->future->data = NULL;
            entry->future->data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            release_pending_shm_handle(service, entry);
#endif
            atomic_set(&entry->future->completed, 1);
            k_sem_give(&entry->future->semaphore);
            entry->future = NULL;
            release_pending_entry(service, entry);
        } else if (entry->callback != NULL) {
            release_pending_entry(service, entry);
        } else if (!entry->completed) {
            entry->canceled = true;
            entry->result = -ECANCELED;
            entry->response_data = NULL;
            entry->response_data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            release_pending_shm_handle(service, entry);
#endif
            k_sem_give(&entry->response_sem);
        }
    }
    ipc_service_pending_unlock(service);

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    /* 反初始化共享内存池（检查泄漏） */
    ipc_shm_deinit(service);
#endif

    LOG_DBG("IPC service '%s' stopped (worker_ret=%d, dispatcher_ret=%d, err=%d)", service->name, ret1, ret2, stop_err);

    return stop_err;
}

/* =============================================================================
 * 调用 API 实现 (Call API Implementation)
 * ============================================================================= */

/**
 * @brief 同步调用实现（可选输入/输出共享内存句柄）
 *
 * @param out_shm_handle 非 NULL 时返回输出句柄；为 NULL 且响应带 shm 时返回 -ENOTSUP
 */
static int ipc_call_sync_impl(ipc_service_t* service, const void* data, size_t data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                              ipc_shm_handle_t in_shm_handle,
#endif
                              void** out_data, size_t* out_data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                              ipc_shm_handle_t* out_shm_handle,
#endif
                              k_timeout_t timeout) {
    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    if (out_data == NULL || out_data_size == NULL) {
        return -EINVAL;
    }

    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

    if (ipc_timeout_is_zero(timeout)) {
        LOG_WRN("ipc_call_sync called with zero timeout");
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = allocate_pending_entry(service);

    if (entry == NULL) {
        ipc_service_pending_unlock(service);
        LOG_ERR("No pending slot available");
        return -ENOMEM;
    }

    init_pending_entry(service, entry, request_id, k_current_get(), NULL, NULL, NULL);

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

    int ret = put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        release_pending_entry(service, entry);
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
            release_pending_entry(service, entry);
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
        release_pending_entry(service, entry);
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

    release_pending_entry(service, entry);
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

/**
 * @brief 同步调用 IPC 服务
 */
int ipc_call_sync(ipc_service_t* service, const void* data, size_t data_size, void** out_data, size_t* out_data_size,
                  k_timeout_t timeout) {
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
    return ipc_call_sync_impl(service, data, data_size, 0, out_data, out_data_size, NULL, timeout);
#else
    return ipc_call_sync_impl(service, data, data_size, out_data, out_data_size, timeout);
#endif
}

#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
/**
 * @brief 同步调用 IPC 服务（带共享内存句柄输入/输出）
 */
int ipc_call_sync_shm(ipc_service_t* service, const void* data, size_t data_size, ipc_shm_handle_t in_shm_handle,
                      void** out_data, size_t* out_data_size, ipc_shm_handle_t* out_shm_handle, k_timeout_t timeout) {
    return ipc_call_sync_impl(service, data, data_size, in_shm_handle, out_data, out_data_size, out_shm_handle,
                              timeout);
}
#endif

/**
 * @brief 异步调用 IPC 服务
 */
int ipc_call_async(ipc_service_t* service, const void* data, size_t data_size, ipc_async_callback_t callback,
                   void* user_data,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                   ipc_shm_handle_t in_shm_handle,
#endif
                   ipc_request_id_t* out_request_id) {
    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    /* SIL-2: data 可以为 NULL（某些服务可能不需要输入数据），但 data_size 必须为 0 */
    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

    if (callback == NULL) {
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    if (out_request_id != NULL) {
        *out_request_id = request_id;
    }

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = allocate_pending_entry(service);

    if (entry == NULL) {
        ipc_service_pending_unlock(service);
        return -ENOMEM;
    }

    /* 初始化为 ASYNC 模式（设置 callback） */
    init_pending_entry(service, entry, request_id, k_current_get(), callback, user_data, NULL);

    ipc_service_pending_unlock(service);

    /* 构建请求消息 */
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

    /* 发送请求到请求队列 */
    int ret = put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        release_pending_entry(service, entry);
        ipc_service_pending_unlock(service);
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
        if (in_shm_handle != 0) {
            ipc_shm_release(service, in_shm_handle);
        }
#endif
        return ret;
    }

    /* 立即返回，回调将在分发线程中执行 */
    return 0;
}

/**
 * @brief Future 模式调用 IPC 服务
 */
int ipc_call_future(ipc_service_t* service, const void* data, size_t data_size,
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
                    ipc_shm_handle_t in_shm_handle,
#endif
                    ipc_future_t** out_future) {
    if (!ipc_service_is_accepting_requests(service)) {
        return -EINVAL;
    }

    /* SIL-2: data 可以为 NULL（某些服务可能不需要输入数据），但 data_size 必须为 0 */
    if (data == NULL && data_size != 0) {
        return -EINVAL;
    }

    if (out_future == NULL) {
        return -EINVAL;
    }

    ipc_request_id_t request_id = ipc_generate_request_id();

    ipc_service_pending_lock(service);

    /* 分配 future 对象 */
    ipc_future_t* future = allocate_future(service);

    if (future == NULL) {
        ipc_service_pending_unlock(service);
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
        ipc_service_pending_unlock(service);
        return -ENOMEM;
    }

    /* 初始化为 FUTURE 模式（设置 future） */
    init_pending_entry(service, entry, request_id, k_current_get(), NULL, NULL, future);

    ipc_service_pending_unlock(service);

    /* 构建请求消息 */
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

    /* 发送请求到请求队列 */
    int ret = put_msgq_until_shutdown(&service->request_queue, &request_msg, &service->shutdown);

    if (ret != 0) {
        ipc_service_pending_lock(service);
        release_pending_entry(service, entry);
        release_future(service, future);
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

/* =============================================================================
 * Future API 实现 (Future API Implementation)
 * ============================================================================= */

/**
 * @brief 等待 Future 结果
 */
int ipc_future_wait(ipc_service_t* service, ipc_future_t* future, int* out_result, const void** out_data,
                    size_t* out_data_size, k_timeout_t timeout) {
    if (future == NULL) {
        return -EINVAL;
    }

    if (service == NULL) {
        return -EINVAL;
    }

    if (!future_belongs_to_service(service, future)) {
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

    ipc_service_pending_lock(service);

    /* 防止重复释放（重复入空闲链表） */
    if (future_is_in_free_list(service, future)) {
        ipc_service_pending_unlock(service);
        return -EALREADY;
    }

    /* 仍被 pending 请求引用时不可释放 */
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use && service->pending_requests[i].future == future) {
            ipc_service_pending_unlock(service);
            return -EBUSY;
        }
    }

    release_future(service, future);
    ipc_service_pending_unlock(service);

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

    ipc_service_pending_lock(service);

    /* 统计 in_use 为 true 的条目数 */
    for (int i = 0; i < CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS; i++) {
        if (service->pending_requests[i].in_use) {
            count++;
        }
    }

    ipc_service_pending_unlock(service);

    return count;
}

/**
 * @brief 取消待处理请求
 */
int ipc_service_cancel(ipc_service_t* service, ipc_request_id_t request_id) {
    if (service == NULL) {
        return -EINVAL;
    }

    ipc_service_pending_lock(service);

    ipc_pending_request_t* entry = find_pending_entry(service, request_id);
    int                    ret = -ENOENT;

    if (entry != NULL) {
        /* SYNC 请求在 response_dispatcher 里可能已经完成，但调用方尚未取走结果。
         * 这类请求不应再被取消，否则会覆盖已完成的响应。 */
        if (entry->completed) {
            ipc_service_pending_unlock(service);
            return -EALREADY;
        }

        /* 根据调用模式采取不同的取消策略 */
        if (entry->future != NULL) {
            /* FUTURE 模式：设置取消状态并唤醒等待者 */
            entry->future->result = -ECANCELED;
            entry->future->data = NULL;
            entry->future->data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            /* 取消时释放共享内存引用 */
            if (entry->shm_handle != 0) {
                ipc_shm_release(service, entry->shm_handle);
                entry->shm_handle = 0;
            }
#endif
            atomic_set(&entry->future->completed, 1);
            k_sem_give(&entry->future->semaphore);
            entry->future = NULL;
            release_pending_entry(service, entry);
            ret = 0;
        } else if (entry->callback != NULL) {
            /* ASYNC 模式：直接释放条目（回调不会被调用） */
            release_pending_entry(service, entry);
            ret = 0;
        } else {
            /* SYNC 模式：设置取消状态并唤醒等待线程 */
            entry->canceled = true;
            entry->result = -ECANCELED;
            entry->response_data = NULL;
            entry->response_data_size = 0;
#if IS_ENABLED(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM)
            release_pending_shm_handle(service, entry);
#endif
            k_sem_give(&entry->response_sem);
            ret = 0;
        }
    }

    ipc_service_pending_unlock(service);

    return ret;
}
