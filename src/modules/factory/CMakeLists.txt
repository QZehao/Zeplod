#
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_FACTORY_MODE_MODULE)
    target_sources(app PRIVATE
        factory_mode.c
    )
    if(CONFIG_FACTORY_MODE_SHELL)
        target_sources(app PRIVATE factory_mode_shell.c)
    endif()
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
