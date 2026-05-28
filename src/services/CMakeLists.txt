#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_SYS_LOG_ENABLE)
    target_sources(app PRIVATE
        sys_log.c
        sys_log.h
    )
endif()

if(CONFIG_SYS_MEMORY_ENABLE)
    target_sources(app PRIVATE
        sys_memory.c
        sys_memory.h
    )
endif()

if(CONFIG_SYS_WATCHDOG_ENABLE)
    target_sources(app PRIVATE
        sys_watchdog.c
        sys_watchdog.h
    )
endif()

if(CONFIG_SYS_TIMER_ENABLE)
    target_sources(app PRIVATE
        sys_timer.c
        sys_timer.h
    )
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
