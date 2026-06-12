#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

target_sources(app PRIVATE
    event_system.c
    event_system_lifecycle.c
    event_system_memory.c
    event_system_pubsub.c
    event_system_publish.c
    event_system_stats.c
    event_queue.c
    event_queue.h
    event_dispatcher.c
    event_dispatcher_autoinit.c
    event_system_compat.c
    event_memory.c
    event_memory.h
    lock_order.c
    state_machine.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
