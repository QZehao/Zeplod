#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_MODULE_MANAGER)
    target_sources(app PRIVATE
        module_manager.c
        module_manager_dependency.c
        module_manager_event.c
        module_manager_lifecycle.c
        module_manager_registry.c
        module_manager_stats.c
        module_manager_compat.c
    )
endif()
