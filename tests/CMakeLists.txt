# 测试 CMakeLists.txt
# 用于构建和运行单元测试（与主应用共享 src/ 下的实现）

cmake_minimum_required(VERSION 3.20.0)

# 与主工程一致：支持从仓库根目录的 zephyr_config.env 加载 ZEPHYR_BASE
if(NOT DEFINED ENV{ZEPHYR_BASE})
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../zephyr_config.env")
		message(STATUS "Loading Zephyr configuration from ../zephyr_config.env...")
		file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../zephyr_config.env" CONFIG_LINES)
		foreach(LINE ${CONFIG_LINES})
			if(NOT LINE MATCHES "^#" AND NOT LINE MATCHES "^$")
				string(REGEX MATCH "^([^=]+)=(.*)$" MATCH ${LINE})
				if(MATCH)
					set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
				endif()
			endif()
		endforeach()
	endif()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zeplod_tests)

enable_testing()

set(APP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# =============================================================================
# 核心源码（始终包含）
# =============================================================================
target_sources(app PRIVATE
	${APP_SRC_DIR}/core/event_system.c
	${APP_SRC_DIR}/core/event_system_lifecycle.c
	${APP_SRC_DIR}/core/event_queue.c
	${APP_SRC_DIR}/core/event_dispatcher.c
	${APP_SRC_DIR}/core/event_memory.c
	${APP_SRC_DIR}/core/event_system_compat.c
	${APP_SRC_DIR}/core/lock_order.c
	${APP_SRC_DIR}/core/state_machine.c
	${APP_SRC_DIR}/services/sys_log.c
	${APP_SRC_DIR}/modules/module_manager.c
	${APP_SRC_DIR}/modules/module_manager_compat.c
)

# =============================================================================
# 核心测试（始终包含）
# =============================================================================
target_sources(app PRIVATE
	test_event_system.c
	test_event_queue.c
	test_event_dispatcher.c
	test_event_memory.c
	test_module_manager.c
	test_sys_log.c
	test_lock_order.c
	test_state_machine.c
)

# 系统内存管理器（可选）
if(CONFIG_SYS_MEMORY_ENABLE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/services/sys_memory.c
		test_sys_memory.c
	)
endif()

# =============================================================================
# 可选服务测试
# =============================================================================

# 看门狗测试
if(CONFIG_SYS_WATCHDOG_ENABLE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/services/sys_watchdog.c
		test_sys_watchdog.c
	)
endif()

# 定时器测试
if(CONFIG_SYS_TIMER_ENABLE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/services/sys_timer.c
		test_sys_timer.c
	)
endif()

# =============================================================================
# 示例模块测试（按配置条件包含）
# =============================================================================

if(CONFIG_EXAMPLE_MODULE_A_ENABLE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules_examples/example_module_a.c
		test_example_module_a.c
	)
endif()

if(CONFIG_EXAMPLE_MODULE_B_ENABLE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules_examples/example_module_b.c
		test_example_module_b.c
	)
endif()

if(CONFIG_EXAMPLE_MODULE_GPIO)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules_examples/example_module_gpio.c
		test_example_module_gpio.c
	)
endif()

if(CONFIG_EXAMPLE_MODULE_UART)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules_examples/example_module_uart.c
		test_example_module_uart.c
	)
endif()

if(CONFIG_EXAMPLE_MODULE_MULTI_DEP)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules_examples/example_module_multi_dep.c
		test_example_module_multi_dep.c
	)
endif()

# =============================================================================
# IPC 服务测试
# =============================================================================
if(CONFIG_THREAD_IPC_SERVICE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules/ipc_service/ipc_service.c
		${APP_SRC_DIR}/modules/ipc_service/ipc_shared_mem.c
		test_ipc_service.c
	)
	target_include_directories(app PRIVATE
		${APP_SRC_DIR}/modules/ipc_service
	)
endif()

# =============================================================================
# Data Bus 测试
# =============================================================================
if(CONFIG_DATA_BUS)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/data_bus/data_bus.c
		${APP_SRC_DIR}/data_bus/data_bus_channel.c
		${APP_SRC_DIR}/data_bus/data_bus_memory.c
		${APP_SRC_DIR}/data_bus/data_bus_consumer.c
		test_data_bus.c
	)
	if(CONFIG_DATA_BUS_EVENT_BRIDGE)
		target_sources(app PRIVATE
			${APP_SRC_DIR}/data_bus/data_bus_event_bridge.c
		)
	endif()
	target_include_directories(app PRIVATE
		${APP_SRC_DIR}/data_bus
	)
endif()

# =============================================================================
# 包含路径
# =============================================================================
target_include_directories(app PRIVATE
	${APP_SRC_DIR}
	${APP_SRC_DIR}/core
	${APP_SRC_DIR}/services
	${APP_SRC_DIR}/modules
	${APP_SRC_DIR}/modules_examples
	${APP_SRC_DIR}/app
)

target_compile_definitions(app PRIVATE
	UNIT_TEST=1
)
