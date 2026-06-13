#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_SYS_LOG_ENABLE)
    target_sources(app PRIVATE sys_log.c)
endif()

if(CONFIG_SYS_MEMORY_ENABLE)
    target_sources(app PRIVATE sys_memory.c)
endif()

if(CONFIG_SYS_WATCHDOG_ENABLE)
    target_sources(app PRIVATE sys_watchdog.c)
endif()

if(CONFIG_SYS_TIMER_ENABLE)
    target_sources(app PRIVATE sys_timer.c)
endif()

if(CONFIG_SYS_DIAG_ENABLE)
    target_sources(app PRIVATE sys_diag.c)
endif()
