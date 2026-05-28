#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

target_sources(app PRIVATE
    app_main.c
    app_main.h
    app_config.h
    app_kv.c
    app_kv.h
    app_version.c
    app_version.h
)

if(CONFIG_SHELL)
    target_sources(app PRIVATE app_shell.c)
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
