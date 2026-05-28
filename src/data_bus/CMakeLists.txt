target_sources(app PRIVATE
    data_bus.c
    data_bus_channel.c
    data_bus_memory.c
    data_bus_consumer.c
    data_bus_ready.c
)

target_sources_ifdef(CONFIG_DATA_BUS_EVENT_BRIDGE app PRIVATE
    data_bus_event_bridge.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
