# Zeplod — Zephyr event-driven application
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0
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

# 版本文件：优先顶层应用仓库的 APP_VERSION（add_subdirectory 时 CMAKE_SOURCE_DIR 为应用根）；
# 否则使用本目录（framework）下的 APP_VERSION（独立构建模板时二者相同）。
if(EXISTS "${CMAKE_SOURCE_DIR}/APP_VERSION")
	file(READ "${CMAKE_SOURCE_DIR}/APP_VERSION" PROJECT_VERSION_RAW)
	string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION_RAW)
	if(PROJECT_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
		set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
		set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
		set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
		set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
		message(STATUS "Project version from \${CMAKE_SOURCE_DIR}/APP_VERSION: ${PROJECT_VERSION}")
	else()
		message(FATAL_ERROR "APP_VERSION must contain a single line X.Y.Z, got: '${PROJECT_VERSION_RAW}'")
	endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/APP_VERSION")
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/APP_VERSION" PROJECT_VERSION_RAW)
	string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION_RAW)
	if(PROJECT_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
		set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
		set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
		set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
		set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
		message(STATUS "Project version from framework/APP_VERSION: ${PROJECT_VERSION}")
	else()
		message(FATAL_ERROR "APP_VERSION must contain a single line X.Y.Z, got: '${PROJECT_VERSION_RAW}'")
	endif()
else()
	set(PROJECT_VERSION_MAJOR 1)
	set(PROJECT_VERSION_MINOR 0)
	set(PROJECT_VERSION_PATCH 0)
	set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
	message(STATUS "Project version default (no APP_VERSION): ${PROJECT_VERSION}")
endif()

message(STATUS "Project Version: ${PROJECT_VERSION}")

# 保存独立的应用版本变量。
# 下方的 find_package(Zephyr) 与 project() 会用 Zephyr 内核版本（如 4.x.x）重写
# PROJECT_VERSION_* / PROJECT_VERSION，因此其后所有编译宏与生成头必须改用 APP_VERSION_*，
# 切勿再复用 PROJECT_VERSION_*，否则固件会错误地报告 Zephyr 版本。
set(APP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(APP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(APP_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(APP_VERSION_STRING "${PROJECT_VERSION}")

# Note: BOARD_ROOT is not needed for standard boards like nucleo_l4r5zi
# Only use BOARD_ROOT if you have custom boards in the boards/ directory
# list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/boards)

# Find Zephyr package
# For freestanding applications, explicitly specify ZEPHYR_BASE
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zeplod VERSION ${APP_VERSION_STRING})

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
add_subdirectory(src/core)

# System Services
add_subdirectory(src/services)

# Application Layer
add_subdirectory(src/app)

# Business Modules (Example - Extendable)
add_subdirectory(src/modules)

# Thread IPC service (Kconfig: CONFIG_THREAD_IPC_SERVICE; avoids Zephyr IPC_SERVICE name clash)
if(CONFIG_THREAD_IPC_SERVICE)
    add_subdirectory(src/modules/ipc_service)
endif()

# Data Bus
if(CONFIG_DATA_BUS)
    add_subdirectory(src/data_bus)
endif()

# Example modules (conditionally compiled)
add_subdirectory(src/modules_examples)

# =============================================================================
# Proprietary Modules (商业闭源模块)
# =============================================================================
# 管理方式：
#   1. 源码模式（默认）：模块自带 CMakeLists.txt + Kconfig，自动管理特性和依赖
#   2. 预编译库模式：提供 .a 文件 + 头文件，适合闭源商业分发
#
# 切换方式（在主 CMakeLists.txt 开头设置）：
# 注意：商业模块目录是可选的，运行 scripts/proprietary_manage.ps1 启用
#
# 极限内存模式：当 CONFIG_SIZE_OPTIMIZATIONS=y 且模块禁用时，跳过商业模块框架
# =============================================================================

# 检查商业模块目录是否存在
set(PROPRIETARY_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/proprietary")
set(PROPRIETARY_MODULES_ENABLED OFF)

# 检查是否应该跳过商业模块（极限内存模式）
if(CONFIG_SIZE_OPTIMIZATIONS)
    # 检查是否有任何商业模块被启用
    set(SKIP_PROPRIETARY ON)
    if(CONFIG_MESH_COMMUNICATION OR CONFIG_MODULE_MANAGER_PRO OR CONFIG_SECURITY_CRYPTO OR
       CONFIG_OTA_MANAGER OR CONFIG_CELLULAR_5G_USB OR
       CONFIG_USB_HOST_CDC_ECM OR CONFIG_USB_HOST_RNDIS OR CONFIG_CLOUD_CONNECTOR)
        set(SKIP_PROPRIETARY OFF)
    endif()
else()
    set(SKIP_PROPRIETARY OFF)
endif()

if(SKIP_PROPRIETARY)
    message(STATUS "Proprietary modules: Skipped (size optimization mode, all modules disabled)")
elseif(EXISTS "${PROPRIETARY_MODULES_DIR}/proprietary_modules_common.cmake")
    set(PROPRIETARY_MODULES_ENABLED ON)
    message(STATUS "Proprietary modules: Enabled")

    # 加载商业模块公共函数
    include("${PROPRIETARY_MODULES_DIR}/proprietary_modules_common.cmake")

    # Proprietary Modules Framework
    target_sources(app PRIVATE
        "${PROPRIETARY_MODULES_DIR}/proprietary_modules.c"
        "${PROPRIETARY_MODULES_DIR}/proprietary_modules.h"
    )
else()
    message(STATUS "Proprietary modules: Disabled (directory not found)")
    message(STATUS "  To enable: Run scripts/proprietary_manage.ps1 -Enable")
endif()

# =============================================================================
# 商业模块加载（统一使用 add_proprietary_module 函数）
# 每个模块内部 CMakeLists.txt 管理：
#   - Kconfig 特性开关（条件编译）
#   - 依赖库链接
#   - 包含目录
#   - 编译定义
# =============================================================================

if(PROPRIETARY_MODULES_ENABLED)
    # Mesh Communication Module (商业模块)
    if(CONFIG_MESH_COMMUNICATION)
        add_proprietary_module(MESH_COMMUNICATION)
    endif()

    # Module Manager Pro Module (商业模块)
    if(CONFIG_MODULE_MANAGER_PRO)
        add_proprietary_module(MODULE_MANAGER_PRO)
    endif()

    # Security Crypto Module (商业模块)
    if(CONFIG_SECURITY_CRYPTO)
        add_proprietary_module(SECURITY_CRYPTO)
    endif()

    # OTA Manager Module (商业模块)
    if(CONFIG_OTA_MANAGER)
        add_proprietary_module(OTA_MANAGER)
    endif()

    # Cellular 5G USB CDC ECM Module (商业模块)
    if(CONFIG_CELLULAR_5G_USB)
        add_proprietary_module(CELLULAR_5G_USB)
    endif()

    # USB Host CDC ECM Module (商业模块)
    if(CONFIG_USB_HOST_CDC_ECM)
        add_proprietary_module(USB_HOST_CDC_ECM)
    endif()

    # USB Host RNDIS Module (商业模块)
    if(CONFIG_USB_HOST_RNDIS)
        add_proprietary_module(USB_HOST_RNDIS)
    endif()

    # Cloud Connector Module (商业模块)
    if(CONFIG_CLOUD_CONNECTOR)
        add_proprietary_module(CLOUD_CONNECTOR)
    endif()

    # 后续商业模块在此添加
endif()

# =============================================================================
# Compiler Definitions
# =============================================================================

target_compile_definitions(app PRIVATE
    PROJECT_VERSION_MAJOR=${APP_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${APP_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${APP_VERSION_PATCH}
    PROJECT_VERSION="${APP_VERSION_STRING}"
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

# 版本信息说明：版本宏（PROJECT_VERSION_* / PROJECT_VERSION / GIT_* / BUILD_*）统一通过
# 上方 target_compile_definitions 的 -D 注入，源头为仓库根 APP_VERSION 文件。
# 不再生成/包含 app_version_config.h（曾为无人 include 的死代码）。
# 对外 API：include/zeplod/*.h；实现细节头文件保留在各 src 子目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/ipc_service
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data_bus
)
