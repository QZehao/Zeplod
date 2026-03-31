# Zephyr Event-Driven Project Template
# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0
#
# This is a freestanding Zephyr application (application and Zephyr are in separate directories)
# See: https://docs.zephyrproject.org/latest/application/index.html#zephyr-freestanding-app

cmake_minimum_required(VERSION 3.20.0)

# =============================================================================
# Load Zephyr Configuration
# =============================================================================

# Check for ZEPHYR_BASE environment variable
# For freestanding applications, ZEPHYR_BASE must point to the Zephyr repository
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

# =============================================================================
# Application version (before find_package(Zephyr))
#
# Do NOT use a file named VERSION at repo root: Zephyr's find_package loads
# cmake/modules/version.cmake which reads VERSION and expects kernel-style fields
# (VERSION_MAJOR etc.); a semver line like 1.0.0 breaks configuration.
# =============================================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/APP_VERSION")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/APP_VERSION" PROJECT_VERSION_RAW)
	string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION_RAW)
	if(PROJECT_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
		set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
		set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
		set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
		set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
	else()
		message(FATAL_ERROR "APP_VERSION must contain a single line X.Y.Z, got: '${PROJECT_VERSION_RAW}'")
	endif()
else()
	set(PROJECT_VERSION_MAJOR 1)
	set(PROJECT_VERSION_MINOR 0)
	set(PROJECT_VERSION_PATCH 0)
	set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
endif()

message(STATUS "Project Version: ${PROJECT_VERSION}")

# Note: BOARD_ROOT is not needed for standard boards like nucleo_l4r5zi
# Only use BOARD_ROOT if you have custom boards in the boards/ directory
# list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/boards)

# Find Zephyr package
# For freestanding applications, explicitly specify ZEPHYR_BASE
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyr_event_template VERSION ${PROJECT_VERSION})

# =============================================================================
# Git Information (Auto-detect)
# =============================================================================

# Find Git
find_package(Git)

if(GIT_FOUND)
    # Get git commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
    )

    if(GIT_RESULT EQUAL 0)
        message(STATUS "Git Commit: ${GIT_COMMIT_HASH}")
    else()
        set(GIT_COMMIT_HASH "unknown")
    endif()

    # Get git branch
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
    )

    if(NOT GIT_RESULT EQUAL 0)
        set(GIT_BRANCH "unknown")
    endif()

    # Get git tag (ERROR_QUIET suppresses stderr; avoid shell redirects for portability)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
    )

    if(NOT GIT_RESULT EQUAL 0)
        set(GIT_TAG "untagged")
    endif()

    # Check for uncommitted changes
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY
        ERROR_QUIET
    )

    if(NOT GIT_DIRTY EQUAL 0)
        set(GIT_DIRTY 1)
        message(STATUS "Git Status: dirty (uncommitted changes)")
    else()
        set(GIT_DIRTY 0)
        message(STATUS "Git Status: clean")
    endif()
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_BRANCH "unknown")
    set(GIT_TAG "unknown")
    set(GIT_DIRTY 0)
    message(STATUS "Git not found, version info will be limited")
endif()

# =============================================================================
# Build Timestamp
# =============================================================================

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP BUILD_TIME "%H:%M:%S")

message(STATUS "Build Timestamp: ${BUILD_TIMESTAMP}")

# =============================================================================
# Target Board
# =============================================================================

if(BOARD)
    set(BUILD_TARGET ${BOARD})
else()
    set(BUILD_TARGET "unknown")
endif()

message(STATUS "Build Target: ${BUILD_TARGET}")

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
    src/app/app_version.c
    src/app/app_version.h
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

# Thread IPC service (Kconfig: CONFIG_THREAD_IPC_SERVICE; avoids Zephyr IPC_SERVICE name clash)
if(CONFIG_THREAD_IPC_SERVICE)
    target_sources(app PRIVATE
        src/modules/ipc_service/ipc_service.c
        src/modules/ipc_service/ipc_service.h
    )
    target_include_directories(app PRIVATE
        ${CMAKE_SOURCE_DIR}/src/modules/ipc_service
    )
endif()

if(CONFIG_THREAD_IPC_SERVICE_EXAMPLE)
    target_sources(app PRIVATE
        src/modules/ipc_service/ipc_service_example.c
    )
endif()

if(CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE)
    target_sources(app PRIVATE
        src/modules/ipc_service/ipc_service_event.c
        src/modules/ipc_service/ipc_service_event.h
    )
endif()

if(CONFIG_EXAMPLE_MODULE_THREAD_IPC)
    target_sources(app PRIVATE
        src/modules/example_module_ipc.c
        src/modules/example_module_ipc.h
    )
endif()

if(CONFIG_EXAMPLE_MODULE_MULTI_DEP)
    target_sources(app PRIVATE
        src/modules/example_module_multi_dep.c
        src/modules/example_module_multi_dep.h
    )
endif()

if(CONFIG_EXAMPLE_MODULE_GPIO)
    target_sources(app PRIVATE
        src/modules/example_module_gpio.c
        src/modules/example_module_gpio.h
    )
endif()

if(CONFIG_EXAMPLE_MODULE_UART)
    target_sources(app PRIVATE
        src/modules/example_module_uart.c
        src/modules/example_module_uart.h
    )
endif()

# =============================================================================
# Compiler Definitions
# =============================================================================

target_compile_definitions(app PRIVATE
    PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PROJECT_VERSION="${PROJECT_VERSION}"
    GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
    GIT_BRANCH="${GIT_BRANCH}"
    GIT_TAG="${GIT_TAG}"
    GIT_DIRTY=${GIT_DIRTY}
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
    BUILD_DATE="${BUILD_DATE}"
    BUILD_TIME="${BUILD_TIME}"
    BUILD_TARGET="${BUILD_TARGET}"
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

# =============================================================================
# Generate Version Header (Optional - for auto-generated version file)
# =============================================================================

# Configure a version header file with build-time values
configure_file(
    ${CMAKE_SOURCE_DIR}/src/app/app_version_config.h.in
    ${CMAKE_BINARY_DIR}/generated/app_version_config.h
    @ONLY
)

target_include_directories(app PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)
