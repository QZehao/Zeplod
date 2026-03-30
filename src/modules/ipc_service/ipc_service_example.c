/*
 * Copyright (c) 2024 Your Name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ipc_service_example.c
 * @brief Example usage of IPC Service Framework
 * 
 * Demonstrates all three invocation modes:
 * 1. SYNC - Blocking call
 * 2. ASYNC - Callback-based
 * 3. FUTURE - Future/Promise pattern
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ipc_service.h"

LOG_MODULE_REGISTER(ipc_example);

/* ============================================================================
 * Example Service Implementation
 * ============================================================================ */

/**
 * @brief Example service function that processes requests
 * 
 * This service simply echoes the input with a computed result.
 */
static int example_service_func(ipc_request_id_t request_id,
                                 const void *data,
                                 size_t data_size,
                                 void **out_data,
                                 size_t *out_data_size)
{
    LOG_INF("Service processing request %u, data_size=%zu", 
            request_id, data_size);
    
    /* Simulate some processing */
    k_msleep(10);
    
    /* For this example, we just return the same data */
    *out_data = (void *)data;
    *out_data_size = data_size;
    
    return 0;
}

/* ============================================================================
 * Global Service Instance
 * ============================================================================ */

static ipc_service_t g_example_service;

/* ============================================================================
 * Mode 1: SYNC Example
 * ============================================================================ */

static void example_sync_call(void)
{
    LOG_INF("=== SYNC Mode Example ===");
    
    const char *input_data = "Hello Sync!";
    void *output_data = NULL;
    size_t output_size = 0;
    
    int result = ipc_call_sync(&g_example_service,
                               input_data,
                               strlen(input_data) + 1,
                               &output_data,
                               &output_size,
                               K_SECONDS(1));
    
    if (result == 0) {
        LOG_INF("SYNC call succeeded: %s", (char *)output_data);
    } else {
        LOG_ERR("SYNC call failed: %d", result);
    }
}

/* ============================================================================
 * Mode 2: ASYNC Example
 * ============================================================================ */

/**
 * @brief Callback for async calls
 */
static void async_callback(ipc_request_id_t request_id,
                           int result,
                           const void *data,
                           size_t data_size,
                           void *user_data)
{
    LOG_INF("ASYNC callback: request_id=%u, result=%d, data=%s, user_data=%p",
            request_id, result, (char *)data, user_data);
}

static void example_async_call(void)
{
    LOG_INF("=== ASYNC Mode Example ===");
    
    const char *input_data = "Hello Async!";
    ipc_request_id_t request_id;
    
    int result = ipc_call_async(&g_example_service,
                                input_data,
                                strlen(input_data) + 1,
                                async_callback,
                                (void *)0x12345678,  /* User data */
                                &request_id);
    
    if (result == 0) {
        LOG_INF("ASYNC call sent, request_id=%u", request_id);
        /* Returns immediately - callback will be invoked later */
    } else {
        LOG_ERR("ASYNC call failed: %d", result);
    }
    
    /* Wait a bit for callback to be invoked */
    k_msleep(100);
}

/* ============================================================================
 * Mode 3: FUTURE Example
 * ============================================================================ */

static void example_future_call(void)
{
    LOG_INF("=== FUTURE Mode Example ===");
    
    const char *input_data = "Hello Future!";
    ipc_future_t *future = NULL;
    
    int result = ipc_call_future(&g_example_service,
                                 input_data,
                                 strlen(input_data) + 1,
                                 &future);
    
    if (result == 0 && future != NULL) {
        LOG_INF("FUTURE call sent, waiting for result...");
        
        /* Option 1: Polling */
        while (!ipc_future_is_ready(future)) {
            k_msleep(10);
        }
        
        /* Option 2: Blocking wait */
        int future_result;
        const void *output_data;
        size_t output_size;
        
        result = ipc_future_wait(future,
                                 &future_result,
                                 &output_data,
                                 &output_size,
                                 K_SECONDS(1));
        
        if (result == 0) {
            LOG_INF("FUTURE call succeeded: result=%d, data=%s",
                    future_result, (char *)output_data);
        } else {
            LOG_ERR("FUTURE wait failed: %d", result);
        }
        
        /* Release the future */
        ipc_future_release(&g_example_service, future);
    } else {
        LOG_ERR("FUTURE call failed: %d", result);
    }
}

/* ============================================================================
 * Test Thread
 * ============================================================================ */

/**
 * @brief Stack for the example thread
 */
static K_THREAD_STACK_DEFINE(example_stack, 2048);

/**
 * @brief Example thread function
 */
static void example_thread_func(void *p1, void *p2, void *p3)
{
    LOG_INF("IPC Service Example Started");
    
    /* Give service time to start */
    k_msleep(100);
    
    /* Run all examples */
    example_sync_call();
    k_msleep(200);
    
    example_async_call();
    k_msleep(200);
    
    example_future_call();
    k_msleep(200);
    
    LOG_INF("IPC Service Example Completed");
    
    /* Print statistics */
    LOG_INF("Pending requests: %zu", 
            ipc_service_get_pending_count(&g_example_service));
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize and start the IPC service
 */
static int ipc_service_example_init(void)
{
    int ret;
    
    /* Initialize service */
    ret = ipc_service_init(&g_example_service,
                           "example_service",
                           example_service_func,
                           CONFIG_IPC_SERVICE_STACK_SIZE,
                           CONFIG_IPC_SERVICE_PRIORITY,
                           CONFIG_IPC_SERVICE_REQUEST_QUEUE_SIZE,
                           CONFIG_IPC_SERVICE_RESPONSE_QUEUE_SIZE);
    
    if (ret != 0) {
        LOG_ERR("Failed to initialize IPC service: %d", ret);
        return ret;
    }
    
    /* Start service */
    ret = ipc_service_start(&g_example_service);
    if (ret != 0) {
        LOG_ERR("Failed to start IPC service: %d", ret);
        return ret;
    }
    
    /* Start example thread */
    static struct k_thread example_thread;
    k_thread_create(&example_thread,
                    example_stack,
                    K_THREAD_STACK_SIZEOF(example_stack),
                    example_thread_func,
                    NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);
    
    LOG_INF("IPC Service Example initialized");
    
    return 0;
}

/* Initialize at boot */
SYS_INIT(ipc_service_example_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
