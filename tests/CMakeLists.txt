# 测试 CMakeLists.txt
# 用于构建和运行单元测试

cmake_minimum_required(VERSION 3.20.0)

# 查找 Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyr_template_tests)

# 启用测试
enable_testing()

# =============================================================================
# 测试文件
# =============================================================================

# 事件系统测试
target_sources(app PRIVATE
    test_event_system.c
    test_event_queue.c
)

# 模块管理器测试
target_sources(app PRIVATE
    test_module_manager.c
)

# 系统服务测试
target_sources(app PRIVATE
    test_sys_memory.c
    test_sys_timer.c
)

# =============================================================================
# 包含目录
# =============================================================================

target_include_directories(app PRIVATE
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/core
    ${CMAKE_SOURCE_DIR}/../src/services
    ${CMAKE_SOURCE_DIR}/../src/modules
)

# =============================================================================
# 编译定义
# =============================================================================

target_compile_definitions(app PRIVATE
    UNIT_TEST=1
    CONFIG_EVENT_QUEUE_SIZE=32
    CONFIG_MAX_MODULES=8
)
