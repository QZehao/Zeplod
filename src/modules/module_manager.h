/**
 * @file module_manager.h
 * @brief Module Manager Header
 * 
 * Dynamic module registration, lifecycle management, and communication.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULE_MANAGER_H
#define MODULE_MANAGER_H

#include "module_base.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Configuration
 * ============================================================================= */

#ifndef CONFIG_MAX_MODULES
#define CONFIG_MAX_MODULES 16
#endif

#ifndef CONFIG_MODULE_INIT_TIMEOUT_MS
#define CONFIG_MODULE_INIT_TIMEOUT_MS 1000
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Module manager event types
 */
typedef enum {
    MODULE_MGR_EVENT_REGISTERED = 0,
    MODULE_MGR_EVENT_UNREGISTERED,
    MODULE_MGR_EVENT_STARTED,
    MODULE_MGR_EVENT_STOPPED,
    MODULE_MGR_EVENT_ERROR,
    MODULE_MGR_EVENT_STATUS_CHANGED
} module_mgr_event_t;

/**
 * @brief Module manager callback
 */
typedef void (*module_mgr_callback_t)(uint32_t module_id,
                                       module_mgr_event_t event,
                                       void *user_data);

/**
 * @brief Module manager statistics
 */
typedef struct {
    uint32_t total_modules;
    uint32_t active_modules;
    uint32_t error_modules;
    uint32_t events_processed;
    uint32_t events_dropped;
} module_mgr_stats_t;

/* =============================================================================
 * Core API
 * ============================================================================= */

/**
 * @brief Initialize module manager
 * @return 0 on success, negative error code on failure
 */
int module_manager_init(void);

/**
 * @brief Start module manager
 * @return 0 on success, negative error code on failure
 */
int module_manager_start(void);

/**
 * @brief Stop module manager
 * @return 0 on success, negative error code on failure
 */
int module_manager_stop(void);

/**
 * @brief Shutdown module manager
 * @return 0 on success, negative error code on failure
 */
int module_manager_shutdown(void);

/* =============================================================================
 * Module Registration API
 * ============================================================================= */

/**
 * @brief Register a module
 * @param interface Module interface
 * @param config Module configuration
 * @param module_id Output: assigned module ID
 * @return 0 on success, negative error code on failure
 *
 * @note init() is invoked while the module manager mutex is held. Do not call
 *       any module_manager_* API from init (deadlock). If init exceeds
 *       CONFIG_MODULE_INIT_TIMEOUT_MS (when > 0), registration fails and
 *       shutdown() is invoked if non-NULL.
 */
int module_manager_register(const module_interface_t *interface,
                            void *config,
                            uint32_t *module_id);

/**
 * @brief Unregister a module
 * @param module_id Module ID
 * @return 0 on success, negative error code on failure
 */
int module_manager_unregister(uint32_t module_id);

/**
 * @brief Copy module info under lock (thread-safe snapshot).
 * @param module_id Module ID
 * @param out Output; must not be NULL
 * @return 0 on success, -1 if not found or invalid args
 */
int module_manager_get_module_info(uint32_t module_id, module_info_t *out);

/**
 * @brief Get module ID by name
 * @param name Module name
 * @return Module ID, 0 if not found
 */
uint32_t module_manager_get_id_by_name(const char *name);

/**
 * @brief Iterate over all modules (snapshots each slot under lock; safe if callback is slow).
 * Do not call back into module_manager from the callback (risk of deadlock with future changes).
 */
void module_manager_foreach(void (*callback)(module_info_t *, void *), 
                            void *user_data);

/* =============================================================================
 * Module Lifecycle API
 * ============================================================================= */

/**
 * @brief Start a module
 * @param module_id Module ID
 * @return 0 on success, negative error code on failure
 */
int module_manager_start_module(uint32_t module_id);

/**
 * @brief Stop a module
 * @param module_id Module ID
 * @return 0 on success, negative error code on failure
 */
int module_manager_stop_module(uint32_t module_id);

/**
 * @brief Start all modules
 * @return Number of modules started successfully
 */
int module_manager_start_all(void);

/**
 * @brief Stop all modules
 * @return Number of modules stopped
 */
int module_manager_stop_all(void);

/**
 * @brief Suspend a module (temporarily disable event handling)
 * @param module_id Module ID
 * @return 0 on success, negative error code on failure
 */
int module_manager_suspend_module(uint32_t module_id);

/**
 * @brief Resume a suspended module
 * @param module_id Module ID
 * @return 0 on success, negative error code on failure
 */
int module_manager_resume_module(uint32_t module_id);

/* =============================================================================
 * Event Handling API
 * ============================================================================= */

/**
 * @brief Subscribe module to event type
 * @param module_id Module ID
 * @param event_type Event type to subscribe to
 * @return 0 on success, negative error code on failure
 */
int module_manager_subscribe(uint32_t module_id, event_type_t event_type);

/**
 * @brief Unsubscribe module from event type
 * @param module_id Module ID
 * @param event_type Event type to unsubscribe from
 * @return 0 on success, negative error code on failure
 */
int module_manager_unsubscribe(uint32_t module_id, event_type_t event_type);

/**
 * @brief Send event to specific module
 * @param module_id Module ID
 * @param event Event to send
 * @return 0 on success, negative error code on failure
 */
int module_manager_send_to_module(uint32_t module_id, const event_t *event);

/**
 * @brief Broadcast event to all modules
 * @param event Event to broadcast
 * @return Number of modules that received the event
 */
int module_manager_broadcast(const event_t *event);

/* =============================================================================
 * Statistics & Debug API
 * ============================================================================= */

/**
 * @brief Get module manager statistics
 * @param stats Output: statistics structure
 */
void module_manager_get_stats(module_mgr_stats_t *stats);

/**
 * @brief Reset module manager statistics
 */
void module_manager_reset_stats(void);

/**
 * @brief Dump module information to console
 */
void module_manager_dump_info(void);

/**
 * @brief Register callback for module events
 * @param callback Callback function
 * @param user_data User data
 */
void module_manager_set_callback(module_mgr_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_MANAGER_H */
