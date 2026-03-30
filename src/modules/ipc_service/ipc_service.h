/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ipc_service.h
 * @brief IPC Service Framework for Zephyr RTOS
 *
 * Provides three invocation modes:
 * - SYNC: Blocking call, waits for result
 * - ASYNC: Non-blocking call with callback
 * - FUTURE: Returns a future object that can be waited on
 *
 * Requires CONFIG_IPC_SERVICE=y (see Kconfig / prj.conf).
 */

#ifndef ZEPHYR_IPC_SERVICE_H_
#define ZEPHYR_IPC_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/slist.h>
#include <stdint.h>
#include <stdbool.h>

#if !IS_ENABLED(CONFIG_IPC_SERVICE)
#error "ipc_service.h requires CONFIG_IPC_SERVICE=y in Kconfig / prj.conf"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ipc_service IPC Service Framework
 * @{
 */

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Request ID type for matching requests with responses
 */
typedef uint32_t ipc_request_id_t;

/**
 * @brief Service function type
 *
 * @param request_id The request identifier
 * @param data Input data pointer
 * @param data_size Size of input data
 * @param out_data Pointer to output data pointer
 * @param out_data_size Pointer to output data size
 * @return int 0 on success, negative errno on failure
 */
typedef int (*ipc_service_func_t)(ipc_request_id_t request_id,
				  const void *data,
				  size_t data_size,
				  void **out_data,
				  size_t *out_data_size);

/**
 * @brief Async callback function type
 *
 * @param request_id The request identifier
 * @param result Result code from service
 * @param data Response data (may be NULL)
 * @param data_size Size of response data
 * @param user_data User data passed during call
 */
typedef void (*ipc_async_callback_t)(ipc_request_id_t request_id,
				     int result,
				     const void *data,
				     size_t data_size,
				     void *user_data);

/**
 * @brief Future handle for promise/future pattern
 */
typedef struct ipc_future {
	ipc_request_id_t request_id; /**< Request ID */
	struct k_sem semaphore;	     /**< Semaphore for waiting */
	int result;		     /**< Result code */
	const void *data;	     /**< Response data */
	size_t data_size;	     /**< Data size */
	bool completed;		     /**< Completion flag */
	struct ipc_future *next;     /**< Next in free list */
} ipc_future_t;

/**
 * @brief Pending request entry
 */
typedef struct ipc_pending_request {
	ipc_request_id_t request_id;    /**< Request ID */
	struct k_thread *caller_thread;	  /**< Calling thread */
	ipc_async_callback_t callback;	  /**< Callback (NULL for sync) */
	void *callback_user_data;	  /**< User data for callback */
	ipc_future_t *future;		  /**< Future (NULL for non-future) */
	struct k_sem response_sem;	  /**< Semaphore for sync wait */
	int result;			  /**< Result code */
	const void *response_data;	  /**< Response data */
	size_t response_data_size;	  /**< Response data size */
	bool in_use;			  /**< Entry is in use */
} ipc_pending_request_t;

/**
 * @brief IPC Service instance
 */
typedef struct ipc_service {
	const char *name; /**< Service name */
	struct k_thread thread;
	struct k_thread response_thread;
	struct k_msgq request_queue;  /**< Request queue */
	struct k_msgq response_queue; /**< Response queue */
	uint8_t *request_queue_buf;
	uint8_t *response_queue_buf;
	void *stack_mem;	       /**< Aligned service thread stack */
	void *dispatcher_stack_mem;    /**< Aligned response dispatcher stack */
	size_t stack_size;	       /**< Bytes per stack (K_THREAD_STACK_LEN) */
	int priority;		       /**< Thread priority */

	ipc_service_func_t service_func; /**< Service function */

	ipc_pending_request_t pending_requests[CONFIG_IPC_SERVICE_MAX_PENDING_REQUESTS];
	struct k_mutex pending_lock;
	ipc_future_t futures[CONFIG_IPC_SERVICE_MAX_PENDING_REQUESTS];
	ipc_future_t *free_futures;

	bool running;	   /**< Service is running */
	volatile bool shutdown; /**< Shutdown requested */
} ipc_service_t;

/**
 * @brief Request message structure
 */
typedef struct ipc_request_msg {
	ipc_request_id_t request_id;
	const void *data;
	size_t data_size;
	ipc_async_callback_t callback;
	void *callback_user_data;
	struct k_thread *caller_thread;
} ipc_request_msg_t;

/**
 * @brief Response message structure
 */
typedef struct ipc_response_msg {
	ipc_request_id_t request_id;
	int result;
	const void *data;
	size_t data_size;
	struct k_thread *caller_thread;
} ipc_response_msg_t;

/* ============================================================================
 * Service Lifecycle APIs
 * ============================================================================ */

int ipc_service_init(ipc_service_t *service,
		     const char *name,
		     ipc_service_func_t service_func,
		     size_t stack_size,
		     int priority,
		     size_t request_queue_size,
		     size_t response_queue_size);

int ipc_service_start(ipc_service_t *service);

int ipc_service_stop(ipc_service_t *service);

/* ============================================================================
 * Invocation APIs - Three Modes
 * ============================================================================ */

int ipc_call_sync(ipc_service_t *service,
		  const void *data,
		  size_t data_size,
		  void **out_data,
		  size_t *out_data_size,
		  k_timeout_t timeout);

int ipc_call_async(ipc_service_t *service,
		   const void *data,
		   size_t data_size,
		   ipc_async_callback_t callback,
		   void *user_data,
		   ipc_request_id_t *out_request_id);

int ipc_call_future(ipc_service_t *service,
		    const void *data,
		    size_t data_size,
		    ipc_future_t **out_future);

int ipc_future_wait(ipc_future_t *future,
		    int *out_result,
		    const void **out_data,
		    size_t *out_data_size,
		    k_timeout_t timeout);

bool ipc_future_is_ready(ipc_future_t *future);

int ipc_future_release(ipc_service_t *service, ipc_future_t *future);

size_t ipc_service_get_pending_count(ipc_service_t *service);

int ipc_service_cancel(ipc_service_t *service, ipc_request_id_t request_id);

ipc_request_id_t ipc_generate_request_id(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_IPC_SERVICE_H_ */
