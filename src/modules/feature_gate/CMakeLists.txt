#
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_FEATURE_GATE_MODULE)
    target_sources(app PRIVATE feature_gate.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
