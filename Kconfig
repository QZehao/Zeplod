# Kconfig for Event-Driven Project Template
# SPDX-License-Identifier: Apache-2.0

menu "Event System Configuration"

config EVENT_SYSTEM
    bool "Enable Event System"
    default y
    help
      Enable the core event-driven architecture for the application.

config EVENT_QUEUE_SIZE
    int "Event Queue Size"
    default 64
    range 16 512
    help
      Maximum number of events that can be queued.

config EVENT_MAX_SUBSCRIBERS
    int "Maximum Event Subscribers"
    default 16
    range 4 64
    help
      Maximum number of subscribers per event type.

config EVENT_DISPATCHER_STACK_SIZE
    int "Event Dispatcher Stack Size"
    default 2048
    range 512 8192
    help
      Stack size for the event dispatcher thread.

config EVENT_DISPATCHER_PRIORITY
    int "Event Dispatcher Priority"
    default 5
    range 1 15
    help
      Priority of the event dispatcher thread (lower = higher priority).

endmenu

menu "Module Manager Configuration"

config MODULE_MANAGER
    bool "Enable Module Manager"
    default y
    help
      Enable dynamic module registration and management.

config MAX_MODULES
    int "Maximum Number of Modules"
    default 16
    range 4 32
    help
      Maximum number of modules that can be registered.

config MODULE_INIT_TIMEOUT_MS
    int "Module Initialization Timeout (ms)"
    default 1000
    range 100 10000
    help
      Timeout for module initialization.

endmenu

menu "System Services Configuration"

config SYS_LOG_LEVEL
    int "System Log Level"
    default 3
    range 0 4
    help
      0=OFF, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG

config SYS_MEMORY_POOL_SIZE
    int "System Memory Pool Size"
    default 8192
    range 1024 65536
    help
      Size of the system memory pool for event allocations.

config SYS_WATCHDOG_ENABLE
    bool "Enable System Watchdog"
    default y
    help
      Enable hardware/software watchdog for system reliability.

config SYS_WATCHDOG_TIMEOUT_MS
    int "Watchdog Timeout (ms)"
    default 5000
    range 1000 30000
    help
      Watchdog timeout in milliseconds.

endmenu
