#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

target_sources(app PRIVATE
    ipc_service.c
    ipc_service_call.c
    ipc_service_dispatcher.c
    ipc_service_lifecycle.c
    ipc_pending_table.c
    ipc_service_worker.c
)

target_sources_ifdef(CONFIG_THREAD_IPC_SERVICE_SHARED_MEM app PRIVATE
    ipc_shared_mem.c
)

target_sources_ifdef(CONFIG_THREAD_IPC_SERVICE_EXAMPLE app PRIVATE
    ipc_service_example.c
)

target_sources_ifdef(CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE app PRIVATE
    ipc_service_event.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
