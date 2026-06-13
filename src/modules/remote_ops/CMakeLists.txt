#
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_REMOTE_OPS_MODULE)
    target_sources(app PRIVATE
        remote_ops.c
    )
    if(CONFIG_REMOTE_OPS_BACKEND_NULL)
        target_sources(app PRIVATE remote_ops_backend_null.c)
    endif()
    if(CONFIG_REMOTE_OPS_SHELL)
        target_sources(app PRIVATE remote_ops_shell.c)
    endif()
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
