/**
 * @file test_state_machine.c
 * @brief Unit tests for state machine helper.
 */

#include <zephyr/ztest.h>

#include <errno.h>

#include "state_machine.h"

ZTEST(state_machine, test_state_machine_progression) {
    zepl_state_machine_t machine;

    zepl_state_machine_init(&machine, ZEP_STATE_UNINIT);

    zassert_equal(zepl_state_machine_get(&machine), ZEP_STATE_UNINIT, NULL);
    zassert_true(zepl_state_can_transition(ZEP_STATE_UNINIT, ZEP_STATE_INITED), NULL);
    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_INITED), 0, NULL);
    zassert_equal(zepl_state_machine_get(&machine), ZEP_STATE_INITED, NULL);

    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_STARTING), 0, NULL);
    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_RUNNING), 0, NULL);
    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_STOPPING), 0, NULL);
    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_STOPPED), 0, NULL);

    zassert_true(zepl_state_is_terminal(ZEP_STATE_STOPPED), NULL);
    zassert_str_equal(zepl_state_name(ZEP_STATE_STOPPED), "STOPPED", NULL);
}

ZTEST(state_machine, test_state_machine_invalid_transition) {
    zepl_state_machine_t machine;

    zepl_state_machine_init(&machine, ZEP_STATE_RUNNING);

    zassert_false(zepl_state_can_transition(ZEP_STATE_RUNNING, ZEP_STATE_INITED), NULL);
    zassert_equal(zepl_state_machine_try_transition(&machine, ZEP_STATE_INITED), -EPERM, NULL);
    zassert_equal(zepl_state_machine_get(&machine), ZEP_STATE_RUNNING, NULL);
}

ZTEST_SUITE(state_machine, NULL, NULL, NULL, NULL, NULL);
