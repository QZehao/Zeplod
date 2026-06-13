#
# Copyright (c) 2026
#
# SPDX-License-Identifier: GPL-3.0
#

if(CONFIG_OTA_MODULE)
    target_sources(app PRIVATE
        ota_module.c
        ota_state_machine.c
    )
    if(CONFIG_OTA_TRANSPORT_NULL)
        target_sources(app PRIVATE ota_transport_null.c)
    endif()
    if(CONFIG_OTA_TRANSPORT_ACTIVE)
        target_sources(app PRIVATE ota_transport_mcuboot.c)
    endif()
    if(CONFIG_OTA_TRANSPORT_MCUMGR_SMP)
        target_sources(app PRIVATE ota_transport_mcumgr_smp.c)
    endif()
    target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
