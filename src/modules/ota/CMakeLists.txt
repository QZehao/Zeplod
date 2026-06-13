#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_OTA_MODULE)
    target_sources(app PRIVATE
        ota_module.c
        ota_state_machine.c
        ota_transport_null.c
    )
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
