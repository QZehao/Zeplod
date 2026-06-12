#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

target_sources(app PRIVATE
    app_main.c
    app_kv.c
    app_version.c
)

# 应用 Shell 命令仅在 CONFIG_APP_ENABLE_SHELL 时编译。
# 该选项 depends on SHELL，故系统 Shell 开启但应用 Shell 关闭时不再注册 app 命令。
if(CONFIG_APP_ENABLE_SHELL)
    target_sources(app PRIVATE app_shell.c)
endif()
