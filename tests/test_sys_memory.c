/**
 * @file test_sys_memory.c
 * @brief sys_memory 单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * Zehao Qian
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zeplod/sys_memory.h>

LOG_MODULE_REGISTER(test_sys_memory);

ZTEST(sys_memory, test_init_and_alloc_free) {
    void* p;

    zassert_equal(sys_mem_init(NULL), 0, "sys_mem_init 失败");

    p = sys_mem_alloc(SYS_MEM_POOL_GENERAL, 32);
    zassert_not_null(p, "alloc 失败");

    sys_mem_free(SYS_MEM_POOL_GENERAL, p);
}

ZTEST(sys_memory, test_zero_size_alloc) {
    zassert_equal(sys_mem_init(NULL), 0, NULL);

    zassert_is_null(sys_mem_alloc(SYS_MEM_POOL_GENERAL, 0), "size 0 应返回 NULL");
}

ZTEST(sys_memory, test_stats) {
    sys_mem_stats_t stats;

    zassert_equal(sys_mem_init(NULL), 0, NULL);

    sys_mem_get_stats(SYS_MEM_POOL_GENERAL, &stats);
    zassert_true(stats.total_size > 0U, "应有池大小");
}

ZTEST_SUITE(sys_memory, NULL, NULL, NULL, NULL, NULL);
