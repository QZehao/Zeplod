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

# 应用 Shell 命令仅在 CONFIG_APP_ENABLE_SHELL 时编译。
# 该选项 depends on SHELL，故系统 Shell 开启但应用 Shell 关闭时不再注册 app 命令。
if(CONFIG_APP_ENABLE_SHELL)
    target_sources(app PRIVATE app_shell.c)
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
