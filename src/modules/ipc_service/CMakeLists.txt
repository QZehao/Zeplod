#
# Copyright (c) 2024 Your Name
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(ipc_service.c)

zephyr_library_sources_ifdef(CONFIG_IPC_SERVICE_EXAMPLE ipc_service_example.c)

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
