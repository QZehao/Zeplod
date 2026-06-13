#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_RECOVERY_POLICY_MODULE)
    target_sources(app PRIVATE recovery_policy.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
