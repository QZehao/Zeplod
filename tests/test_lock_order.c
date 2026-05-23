/**
 * @file test_lock_order.c
 * @brief Unit tests for lock ordering helper.
 */

#include <zephyr/ztest.h>

#include "lock_order.h"

static void lock_order_suite_teardown(void* fixture) {
    ARG_UNUSED(fixture);
    zepl_lock_reset_current_thread();
}

ZTEST(lock_order, test_valid_lock_order_sequence) {
    zepl_lock_reset_current_thread();

    zassert_true(zepl_lock_order_is_valid(ZEP_LOCK_LEVEL_STATE, 0x100U), NULL);
    zepl_lock_enter(ZEP_LOCK_LEVEL_STATE, 0x100U);
    zassert_equal(zepl_lock_current_depth(), 1U, NULL);
    zassert_equal(zepl_lock_current_level(), ZEP_LOCK_LEVEL_STATE, NULL);

    zassert_true(zepl_lock_order_is_valid(ZEP_LOCK_LEVEL_STATE, 0x200U), NULL);
    zepl_lock_enter(ZEP_LOCK_LEVEL_STATE, 0x200U);
    zassert_equal(zepl_lock_current_depth(), 2U, NULL);

    zassert_true(zepl_lock_order_is_valid(ZEP_LOCK_LEVEL_TABLE, 0x050U), NULL);
    zepl_lock_enter(ZEP_LOCK_LEVEL_TABLE, 0x050U);
    zassert_equal(zepl_lock_current_depth(), 3U, NULL);
    zassert_equal(zepl_lock_current_level(), ZEP_LOCK_LEVEL_TABLE, NULL);

    zepl_lock_exit(ZEP_LOCK_LEVEL_TABLE, 0x050U);
    zepl_lock_exit(ZEP_LOCK_LEVEL_STATE, 0x200U);
    zepl_lock_exit(ZEP_LOCK_LEVEL_STATE, 0x100U);

    zassert_equal(zepl_lock_current_depth(), 0U, NULL);
}

ZTEST(lock_order, test_invalid_lock_order_sequence) {
    zepl_lock_reset_current_thread();

    zepl_lock_enter(ZEP_LOCK_LEVEL_TABLE, 0x200U);

    zassert_false(zepl_lock_order_is_valid(ZEP_LOCK_LEVEL_STATE, 0x100U), NULL);
    zassert_false(zepl_lock_order_is_valid(ZEP_LOCK_LEVEL_TABLE, 0x100U), NULL);

    zepl_lock_exit(ZEP_LOCK_LEVEL_TABLE, 0x200U);
}

ZTEST_SUITE(lock_order, NULL, NULL, NULL, NULL, lock_order_suite_teardown);
