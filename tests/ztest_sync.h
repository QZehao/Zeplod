/**
 * @file ztest_sync.h
 * @brief 单元测试用有界异步等待 helper（避免裸 k_msleep 判断完成）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#ifndef ZTEST_SYNC_H
#define ZTEST_SYNC_H

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 等待原子变量等于期望值（轮询间隔 1ms）
 *
 * @param var 原子变量
 * @param expected 期望值
 * @param timeout_ms 超时（毫秒）
 * @return true 在超时前达到期望；false 超时
 */
static inline bool ztest_wait_atomic_eq(atomic_t* var, atomic_val_t expected, uint32_t timeout_ms) {
    uint32_t elapsed = 0U;

    while (elapsed < timeout_ms) {
        if (atomic_get(var) == expected) {
            return true;
        }
        k_sleep(K_MSEC(1));
        elapsed++;
    }
    return atomic_get(var) == expected;
}

/**
 * @brief 等待原子变量非零
 */
static inline bool ztest_wait_atomic_nonzero(atomic_t* var, uint32_t timeout_ms) {
    uint32_t elapsed = 0U;

    while (elapsed < timeout_ms) {
        if (atomic_get(var) != 0) {
            return true;
        }
        k_sleep(K_MSEC(1));
        elapsed++;
    }
    return atomic_get(var) != 0;
}

/**
 * @brief 等待信号量（带超时）
 */
static inline bool ztest_wait_sem(struct k_sem* sem, k_timeout_t timeout) {
    return k_sem_take(sem, timeout) == 0;
}

#endif /* ZTEST_SYNC_H */
