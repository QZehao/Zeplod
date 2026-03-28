# Zephyr Event-Driven Project Template
# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# =============================================================================
# Load Zephyr Configuration
# =============================================================================

# Check for ZEPHYR_BASE environment variable
if(NOT DEFINED ENV{ZEPHYR_BASE})
    # Try to load from zephyr_config.env file
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zephyr_config.env")
        message(STATUS "Loading Zephyr configuration from zephyr_config.env...")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/zephyr_config.env" CONFIG_LINES)
        foreach(LINE ${CONFIG_LINES})
            # Skip comments and empty lines
            if(NOT LINE MATCHES "^#" AND NOT LINE MATCHES "^$")
                string(REGEX MATCH "^([^=]+)=(.*)$" MATCH ${LINE})
                if(MATCH)
                    set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
                endif()
            endif()
        endforeach()
    else()
        message(FATAL_ERROR 
            "ZEPHYR_BASE not set and zephyr_config.env not found.\n"
            "Please either:\n"
            "  1. Set ZEPHYR_BASE environment variable, or\n"
            "  2. Copy zephyr_config.env.template to zephyr_config.env and edit paths,\n"
            "  3. Run scripts/setup_env.bat (Windows) or source scripts/setup_env.sh (Linux/macOS)"
        )
    endif()
endif()

message(STATUS "ZEPHYR_BASE: $ENV{ZEPHYR_BASE}")
message(STATUS "ZEPHYR_SDK_INSTALL_DIR: $ENV{ZEPHYR_SDK_INSTALL_DIR}")

# Find Zephyr SDK
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyr_event_template)

# =============================================================================
# Project Source Files
# =============================================================================

# Core Event System
target_sources(app PRIVATE
    src/core/event_system.c
    src/core/event_system.h
    src/core/event_queue.c
    src/core/event_queue.h
    src/core/event_dispatcher.c
    src/core/event_dispatcher.h
)

# System Services
target_sources(app PRIVATE
    src/services/sys_log.c
    src/services/sys_log.h
    src/services/sys_memory.c
    src/services/sys_memory.h
    src/services/sys_watchdog.c
    src/services/sys_watchdog.h
    src/services/sys_timer.c
    src/services/sys_timer.h
)

# Application Layer - Base
target_sources(app PRIVATE
    src/app/app_main.c
    src/app/app_main.h
    src/app/app_config.h
)

# Business Modules (Example - Extendable)
target_sources(app PRIVATE
    src/modules/module_manager.c
    src/modules/module_manager.h
    src/modules/module_base.h
    # Example modules
    src/modules/example_module_a.c
    src/modules/example_module_a.h
    src/modules/example_module_b.c
    src/modules/example_module_b.h
)

# =============================================================================
# Compiler Definitions
# =============================================================================

target_compile_definitions(app PRIVATE
    PROJECT_VERSION_MAJOR=1
    PROJECT_VERSION_MINOR=0
    PROJECT_VERSION_PATCH=0
)

# =============================================================================
# Include Directories
# =============================================================================

target_include_directories(app PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/services
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_SOURCE_DIR}/src/modules
)
