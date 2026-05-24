/**
 * @file test_event_memory.c
 * @brief 事件内存管理模块单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-17
 *
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-04-17       1.0            zeh            初始版本
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include "event_memory.h"
#include "event_system.h"

LOG_MODULE_REGISTER(test_event_memory);

/* =============================================================================
 * Slab 选择函数测试
 * ============================================================================= */

/**
 * @brief 测试事件 Slab 选择 - NORMAL 优先级
 */
ZTEST(test_event_memory, test_select_event_slab_normal) {
    struct k_mem_slab* slab;

    slab = event_memory_select_event_slab(EVENT_PRIORITY_NORMAL);
#if EVENT_SLAB_ENABLED
    zassert_not_null(slab, "NORMAL 优先级应返回有效 Slab");
#else
    zassert_is_null(slab, "Slab 未启用时应返回 NULL");
#endif
}

/**
 * @brief 测试事件 Slab 选择 - LOW 优先级
 */
ZTEST(test_event_memory, test_select_event_slab_low) {
    struct k_mem_slab* slab;

    slab = event_memory_select_event_slab(EVENT_PRIORITY_LOW);
#if EVENT_SLAB_ENABLED
    /* LOW 优先级通常回退到 NORMAL 池 */
    zassert_not_null(slab, "LOW 优先级应返回有效 Slab");
#else
    zassert_is_null(slab, "Slab 未启用时应返回 NULL");
#endif
}

/**
 * @brief 测试事件 Slab 选择 - HIGH 优先级
 */
ZTEST(test_event_memory, test_select_event_slab_high) {
    struct k_mem_slab* slab;

    slab = event_memory_select_event_slab(EVENT_PRIORITY_HIGH);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_HIGH_AVAILABLE
    zassert_not_null(slab, "HIGH 优先级应返回有效 Slab");
#elif EVENT_SLAB_ENABLED
    /* 如果没有 HIGH 池，应回退到 NORMAL 池 */
    zassert_not_null(slab, "应回退到 NORMAL Slab");
#else
    zassert_is_null(slab, "Slab 未启用时应返回 NULL");
#endif
}

/**
 * @brief 测试事件 Slab 选择 - CRITICAL 优先级
 */
ZTEST(test_event_memory, test_select_event_slab_critical) {
    struct k_mem_slab* slab;

    slab = event_memory_select_event_slab(EVENT_PRIORITY_CRITICAL);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_CRITICAL_AVAILABLE
    zassert_not_null(slab, "CRITICAL 优先级应返回有效 Slab");
#elif EVENT_SLAB_ENABLED
    /* 如果没有 CRITICAL 池，应回退 */
    zassert_not_null(slab, "应回退到可用 Slab");
#else
    zassert_is_null(slab, "Slab 未启用时应返回 NULL");
#endif
}

/* =============================================================================
 * 数据 Slab 选择函数测试
 * ============================================================================= */

/**
 * @brief 测试数据 Slab 选择 - 小数据
 */
ZTEST(test_event_memory, test_select_data_slab_small) {
    struct k_mem_slab* slab;

    /* 0 字节应返回 NULL */
    slab = event_memory_select_data_slab(0);
    zassert_is_null(slab, "0 字节应返回 NULL");

    /* 小于内联数据大小的数据 */
    slab = event_memory_select_data_slab(16);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_256_AVAILABLE
    zassert_not_null(slab, "小数据应返回 256B Slab");
#else
    /* 取决于配置 */
#endif
}

/**
 * @brief 测试数据 Slab 选择 - 256 字节边界
 */
ZTEST(test_event_memory, test_select_data_slab_256_boundary) {
    struct k_mem_slab* slab;

    /* 恰好 256 字节 */
    slab = event_memory_select_data_slab(256);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_256_AVAILABLE
    zassert_not_null(slab, "256 字节应返回 256B Slab");
#endif

    /* 257 字节（超过 256）*/
    slab = event_memory_select_data_slab(257);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_1K_AVAILABLE
    zassert_not_null(slab, "257 字节应返回 1K Slab");
#elif EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_256_AVAILABLE
    zassert_is_null(slab, "超过 256B 但无 1K 池应返回 NULL");
#endif
}

/**
 * @brief 测试数据 Slab 选择 - 1KB 边界
 */
ZTEST(test_event_memory, test_select_data_slab_1k_boundary) {
    struct k_mem_slab* slab;

    /* 恰好 1024 字节 */
    slab = event_memory_select_data_slab(1024);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_1K_AVAILABLE
    zassert_not_null(slab, "1024 字节应返回 1K Slab");
#endif

    /* 1025 字节 */
    slab = event_memory_select_data_slab(1025);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_4K_AVAILABLE
    zassert_not_null(slab, "1025 字节应返回 4K Slab");
#endif
}

/**
 * @brief 测试数据 Slab 选择 - 4KB 边界
 */
ZTEST(test_event_memory, test_select_data_slab_4k_boundary) {
    struct k_mem_slab* slab;

    /* 恰好 4096 字节 */
    slab = event_memory_select_data_slab(4096);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_LARGE_AVAILABLE && EVENT_SLAB_4K_AVAILABLE
    zassert_not_null(slab, "4096 字节应返回 4K Slab");
#endif

    /* 超过 4KB */
    slab = event_memory_select_data_slab(4097);
    zassert_is_null(slab, "超过 4KB 应返回 NULL（需要 k_malloc）");
}

/**
 * @brief 测试数据 Slab 选择 - 大数据
 */
ZTEST(test_event_memory, test_select_data_slab_large) {
    struct k_mem_slab* slab;

    /* 10KB 数据 */
    slab = event_memory_select_data_slab(10 * 1024);
    zassert_is_null(slab, "10KB 数据应返回 NULL（超出 Slab 范围）");

    /* 1MB 数据 */
    slab = event_memory_select_data_slab(1024 * 1024);
    zassert_is_null(slab, "1MB 数据应返回 NULL");
}

/* =============================================================================
 * 运行时状态 API 测试
 * ============================================================================= */

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)

/**
 * @brief 测试 Slab 可用性检查
 */
ZTEST(test_event_memory, test_slab_available) {
    bool available;

    /* NORMAL 优先级 */
    available = event_slab_available(EVENT_PRIORITY_NORMAL);
#if EVENT_SLAB_ENABLED
    zassert_true(available, "NORMAL Slab 应可用");
#else
    zassert_false(available, "Slab 未启用时应返回 false");
#endif

    /* CRITICAL 优先级 */
    available = event_slab_available(EVENT_PRIORITY_CRITICAL);
#if EVENT_SLAB_ENABLED && EVENT_SLAB_CRITICAL_AVAILABLE
    /* 初始状态应可用 */
#elif EVENT_SLAB_ENABLED
    /* 回退到 NORMAL，取决于 NORMAL 池状态 */
#endif
}

/**
 * @brief 测试 Slab 剩余块数
 */
ZTEST(test_event_memory, test_slab_remaining) {
    uint32_t remaining;

    remaining = event_slab_remaining(EVENT_PRIORITY_NORMAL);
#if EVENT_SLAB_ENABLED
    zassert_true(remaining > 0, "NORMAL Slab 应有剩余块");
    zassert_true(remaining <= CONFIG_EVENT_SLAB_NORMAL_COUNT, "剩余块不应超过总数");
#else
    zassert_equal(remaining, 0, "Slab 未启用时应返回 0");
#endif
}

/**
 * @brief 测试获取 Slab 统计信息
 */
ZTEST(test_event_memory, test_get_slab_stats) {
    event_slab_stats_t stats;

    event_get_slab_stats(&stats);

#if EVENT_SLAB_ENABLED
    /* NORMAL 池必须存在 */
    zassert_true(stats.normal_total >= 4, "NORMAL 池至少应有 4 块");
    zassert_true(stats.normal_used <= stats.normal_total, "已用块不应超过总数");

#if EVENT_SLAB_CRITICAL_AVAILABLE
    zassert_true(stats.critical_total > 0, "CRITICAL 池应存在");
#endif

#if EVENT_SLAB_HIGH_AVAILABLE
    zassert_true(stats.high_total > 0, "HIGH 池应存在");
#endif

#if EVENT_SLAB_256_AVAILABLE
    zassert_true(stats.data_256_total > 0, "256B 数据池应存在");
#endif

#if EVENT_SLAB_1K_AVAILABLE
    zassert_true(stats.data_1k_total > 0, "1K 数据池应存在");
#endif

#if EVENT_SLAB_4K_AVAILABLE
    zassert_true(stats.data_4k_total > 0, "4K 数据池应存在");
#endif
#else
    /* Slab 未启用时，统计值应全为 0 */
    zassert_equal(stats.normal_total, 0, "Slab 未启用时应为 0");
#endif
}

/**
 * @brief 测试 Slab 统计 NULL 参数
 */
ZTEST(test_event_memory, test_get_slab_stats_null) {
    /* 不应崩溃 */
    event_get_slab_stats(NULL);
}

/**
 * @brief 测试 Slab 耗尽后的统计
 */
ZTEST(test_event_memory, test_slab_stats_after_allocation) {
    event_slab_stats_t stats_before, stats_after;
    event_t*           events[8];
    int                allocated = 0;

    event_get_slab_stats(&stats_before);

    /* 分配一些事件 */
    for (int i = 0; i < 8; i++) {
        events[i] = event_create_rt(100 + i, EVENT_PRIORITY_NORMAL);
        if (events[i] != NULL) {
            allocated++;
        }
    }

    event_get_slab_stats(&stats_after);

#if EVENT_SLAB_ENABLED
    zassert_true(stats_after.normal_used >= stats_before.normal_used + allocated, "已用块应增加");
#endif

    /* 释放事件 */
    for (int i = 0; i < allocated; i++) {
        event_free(events[i]);
    }
}

#endif /* CONFIG_EVENT_RUNTIME_STATUS */

/* =============================================================================
 * Slab 耗尽回调测试
 * ============================================================================= */

#if defined(CONFIG_EVENT_SLAB_EXHAUSTED_CB) && (CONFIG_EVENT_SLAB_EXHAUSTED_CB == 1)

static int              g_exhausted_callback_count = 0;
static event_priority_t g_last_exhausted_priority = 0;

static void test_exhausted_callback(event_priority_t priority, const char* slab_name) {
    g_exhausted_callback_count++;
    g_last_exhausted_priority = priority;
    LOG_INF("Slab exhausted: priority=%d, slab=%s", priority, slab_name);
}

/**
 * @brief 测试注册 Slab 耗尽回调
 */
ZTEST(test_event_memory, test_register_exhausted_callback) {
    g_exhausted_callback_count = 0;

    /* 注册回调 */
    event_register_slab_exhausted_cb(test_exhausted_callback);

    /* 清除回调 */
    event_register_slab_exhausted_cb(NULL);
}

/**
 * @brief 测试清除 Slab 耗尽回调
 */
ZTEST(test_event_memory, test_clear_exhausted_callback) {
    g_exhausted_callback_count = 0;

    /* 注册回调 */
    event_register_slab_exhausted_cb(test_exhausted_callback);

    /* 清除回调 */
    event_register_slab_exhausted_cb(NULL);

    /* 尝试触发耗尽（如果有足够内存，可能不会触发）*/
    /* 这里只是验证不会崩溃 */
}

#endif /* CONFIG_EVENT_SLAB_EXHAUSTED_CB */

/* =============================================================================
 * 内存调试 API 测试
 * ============================================================================= */

#if defined(CONFIG_EVENT_DEBUG_MEM) && (CONFIG_EVENT_DEBUG_MEM == 1)

/**
 * @brief 测试内存泄漏检测
 */
ZTEST(test_event_memory, test_check_leaks) {
    uint32_t leaks;
    event_t* event = NULL;

    /* 初始状态应无泄漏 */
    leaks = event_check_leaks();
    zassert_equal(leaks, 0, "初始状态应无泄漏");

    /* 创建事件但不释放 */
    event = event_create(200, EVENT_PRIORITY_NORMAL);
    if (event != NULL) {
        leaks = event_check_leaks();
        zassert_true(leaks >= 1, "创建事件后应有泄漏");

        /* 释放事件 */
        event_free(event);
        leaks = event_check_leaks();
        zassert_equal(leaks, 0, "释放后应无泄漏");
    }
}

/**
 * @brief 测试打印内存泄漏详情
 */
ZTEST(test_event_memory, test_dump_leaks) {
    event_t* event = NULL;

    /* 无泄漏时打印 */
    event_dump_leaks();

    /* 创建事件后打印 */
    event = event_create(201, EVENT_PRIORITY_NORMAL);
    if (event != NULL) {
        event_dump_leaks();
        event_free(event);
    }
}

#endif /* CONFIG_EVENT_DEBUG_MEM */

/* =============================================================================
 * 与事件系统集成测试
 * ============================================================================= */

/**
 * @brief 测试通过事件系统分配的事件使用 Slab
 */
ZTEST(test_event_memory, test_event_create_rt_integration) {
    event_t* event;

    event = event_create_rt(210, EVENT_PRIORITY_NORMAL);

    if (event != NULL) {
        /* 验证事件属性 */
        zassert_equal(event->type, 210, "事件类型不匹配");
        zassert_equal(event->priority, EVENT_PRIORITY_NORMAL, "优先级不匹配");
        zassert_true((event->flags & EVENT_FLAG_FROM_SLAB) != 0, "应标记为来自 Slab");

        event_free(event);
    }
}

/**
 * @brief 测试带数据的实时安全创建
 */
ZTEST(test_event_memory, test_event_create_with_data_rt_integration) {
    event_t* event;
    uint8_t  data[32];

    for (int i = 0; i < 32; i++) {
        data[i] = (uint8_t) (i * 7);
    }

    event = event_create_with_data_rt(220, EVENT_PRIORITY_HIGH, data, sizeof(data));

    if (event != NULL) {
        /* 验证事件属性 */
        zassert_equal(event->type, 220, "事件类型不匹配");
        zassert_equal(event->priority, EVENT_PRIORITY_HIGH, "优先级不匹配");
        zassert_equal(event->data_len, sizeof(data), "数据长度不匹配");

        /* 验证数据 */
        zassert_mem_equal(event->data.inline_data, data, sizeof(data), "数据内容不匹配");

        event_free(event);
    }
}

/**
 * @brief 测试大量分配后的 Slab 状态
 */
ZTEST(test_event_memory, test_mass_allocation) {
#define MASS_ALLOC_COUNT 16
    event_t* events[MASS_ALLOC_COUNT];
    int      allocated = 0;

    /* 大量分配 */
    for (int i = 0; i < MASS_ALLOC_COUNT; i++) {
        events[i] = event_create_rt(230 + i, EVENT_PRIORITY_NORMAL);
        if (events[i] != NULL) {
            allocated++;
        }
    }

    /* 应该成功分配一些事件 */
    zassert_true(allocated > 0, "应至少分配一个事件");

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)
    /* 检查 Slab 状态 */
    event_slab_stats_t stats;
    event_get_slab_stats(&stats);
    zassert_true(stats.normal_used >= (uint32_t) allocated, "已用块数应正确");
#endif

    /* 全部释放 */
    for (int i = 0; i < allocated; i++) {
        event_free(events[i]);
    }

#if defined(CONFIG_EVENT_RUNTIME_STATUS) && (CONFIG_EVENT_RUNTIME_STATUS == 1)
    /* 验证释放后的状态 */
    event_get_slab_stats(&stats);
    /* 已用块应减少 */
#endif
}

/**
 * @brief 测试不同优先级混合分配
 */
ZTEST(test_event_memory, test_mixed_priority_allocation) {
    event_t* events[12];
    int      allocated = 0;

    /* 混合优先级分配 */
    events[allocated] = event_create_rt(240, EVENT_PRIORITY_CRITICAL);
    if (events[allocated] != NULL)
        allocated++;

    events[allocated] = event_create_rt(241, EVENT_PRIORITY_HIGH);
    if (events[allocated] != NULL)
        allocated++;

    events[allocated] = event_create_rt(242, EVENT_PRIORITY_NORMAL);
    if (events[allocated] != NULL)
        allocated++;

    events[allocated] = event_create_rt(243, EVENT_PRIORITY_LOW);
    if (events[allocated] != NULL)
        allocated++;

    /* 再分配一些 */
    for (int i = 0; i < 8; i++) {
        events[allocated] = event_create_rt(250 + i, EVENT_PRIORITY_NORMAL);
        if (events[allocated] != NULL)
            allocated++;
    }

    /* 全部释放 */
    for (int i = 0; i < allocated; i++) {
        event_free(events[i]);
    }
}

/* =============================================================================
 * 测试套件
 * ============================================================================= */

ZTEST_SUITE(test_event_memory, NULL, NULL, NULL, NULL, NULL);
