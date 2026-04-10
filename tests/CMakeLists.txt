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

project(zephyr_template_tests)

enable_testing()

set(APP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# 被测实现（与主应用核心一致；不包含 app_main / 示例业务模块）
target_sources(app PRIVATE
	${APP_SRC_DIR}/core/event_system.c
	${APP_SRC_DIR}/core/event_queue.c
	${APP_SRC_DIR}/core/event_dispatcher.c
	${APP_SRC_DIR}/core/event_system_compat.c  # 事件系统兼容层
	${APP_SRC_DIR}/services/sys_log.c
	${APP_SRC_DIR}/services/sys_memory.c
	${APP_SRC_DIR}/services/sys_watchdog.c
	${APP_SRC_DIR}/services/sys_timer.c
	${APP_SRC_DIR}/modules/module_manager.c
	${APP_SRC_DIR}/modules/module_manager_compat.c  # 模块管理器兼容层
)

target_sources(app PRIVATE
	test_event_system.c
	test_event_queue.c
	test_event_dispatcher.c
	test_module_manager.c
	test_sys_memory.c
	test_sys_timer.c
	test_sys_watchdog.c
	test_sys_log.c
	test_example_module_a.c
	test_example_module_b.c
	test_example_module_gpio.c
	test_example_module_uart.c
	test_example_module_multi_dep.c
)

if(CONFIG_THREAD_IPC_SERVICE)
	target_sources(app PRIVATE
		${APP_SRC_DIR}/modules/ipc_service/ipc_service.c
		test_ipc_service.c
	)
	target_include_directories(app PRIVATE
		${APP_SRC_DIR}/modules/ipc_service
	)
endif()

target_include_directories(app PRIVATE
	${APP_SRC_DIR}
	${APP_SRC_DIR}/core
	${APP_SRC_DIR}/services
	${APP_SRC_DIR}/modules
	${APP_SRC_DIR}/app
)

# 示例模块源码（用于单元测试）
target_sources(app PRIVATE
	${APP_SRC_DIR}/modules/example_module_a.c
	${APP_SRC_DIR}/modules/example_module_b.c
	${APP_SRC_DIR}/modules/example_module_gpio.c
	${APP_SRC_DIR}/modules/example_module_uart.c
	${APP_SRC_DIR}/modules/example_module_multi_dep.c
)

target_compile_definitions(app PRIVATE
	UNIT_TEST=1
)
