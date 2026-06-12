#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_EXAMPLE_MODULE_A_ENABLE)
    target_sources(app PRIVATE example_module_a.c)
endif()

if(CONFIG_EXAMPLE_MODULE_B_ENABLE)
    target_sources(app PRIVATE example_module_b.c)
endif()

if(CONFIG_EXAMPLE_MODULE_THREAD_IPC)
    target_sources(app PRIVATE example_module_ipc.c)
endif()

if(CONFIG_EXAMPLE_MODULE_MULTI_DEP)
    target_sources(app PRIVATE example_module_multi_dep.c)
endif()

if(CONFIG_EXAMPLE_MODULE_GPIO)
    target_sources(app PRIVATE example_module_gpio.c)
endif()

if(CONFIG_EXAMPLE_MODULE_UART)
    target_sources(app PRIVATE example_module_uart.c)
endif()
