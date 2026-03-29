/**
 * @file module_base.h
 * @brief Base Module Interface Header
 * 
 * Abstract interface for all business modules.
 * 
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODULE_BASE_H
#define MODULE_BASE_H

#include "event_system.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Type Definitions
 * ============================================================================= */

/**
 * @brief Module status
 */
typedef enum {
    MODULE_STATUS_UNINITIALIZED = 0,
    MODULE_STATUS_INITIALIZING,
    MODULE_STATUS_INITIALIZED,
    MODULE_STATUS_RUNNING,
    MODULE_STATUS_STOPPED,
    MODULE_STATUS_ERROR,
    MODULE_STATUS_SUSPENDED
} module_status_t;

/**
 * @brief Module priority levels
 */
typedef enum {
    MODULE_PRIORITY_LOW      = 10,
    MODULE_PRIORITY_NORMAL   = 5,
    MODULE_PRIORITY_HIGH     = 2,
    MODULE_PRIORITY_CRITICAL = 0
} module_priority_t;

/**
 * @brief Module event handler
 * @param event Pointer to received event
 * @param user_data Module-specific data
 */
typedef void (*module_event_handler_t)(const event_t *event, void *user_data);

/**
 * @brief Module interface (virtual table)
 */
typedef struct {
    /** Module name */
    const char *name;
    
    /** Module version (MAJOR.MINOR.PATCH encoded) */
    uint32_t version;
    
    /** Module priority */
    module_priority_t priority;
    
    /** Initialize module */
    int (*init)(void *config);
    
    /** Start module */
    int (*start)(void);
    
    /** Stop module */
    int (*stop)(void);
    
    /** Shutdown module */
    int (*shutdown)(void);
    
    /** Handle events */
    module_event_handler_t on_event;
    
    /** Get module status */
    module_status_t (*get_status)(void);
    
    /** Module-specific control */
    int (*control)(int cmd, void *arg);
} module_interface_t;

/**
 * @brief Module registration info
 */
typedef struct {
    const module_interface_t *interface;
    void *config;
    void *internal_data;
    module_status_t status;
    uint32_t id;
    uint32_t event_subscriber_id;
} module_info_t;

/* =============================================================================
 * Module Helper Macros
 * ============================================================================= */

#define MODULE_VERSION(major, minor, patch) \
    (((major) << 16) | ((minor) << 8) | (patch))

#define MODULE_VERSION_MAJOR(v)  (((v) >> 16) & 0xFF)
#define MODULE_VERSION_MINOR(v)  (((v) >> 8) & 0xFF)
#define MODULE_VERSION_PATCH(v)  ((v) & 0xFF)

#define DECLARE_MODULE_INTERFACE(name) \
    extern const module_interface_t name##_interface; \
    const module_interface_t name##_interface = { \
        .name = #name, \
        .version = MODULE_VERSION(1, 0, 0), \
        .priority = MODULE_PRIORITY_NORMAL, \
        .init = name##_init, \
        .start = name##_start, \
        .stop = name##_stop, \
        .shutdown = name##_shutdown, \
        .on_event = name##_on_event, \
        .get_status = name##_get_status, \
        .control = name##_control \
    }

#ifdef __cplusplus
}
#endif

#endif /* MODULE_BASE_H */
