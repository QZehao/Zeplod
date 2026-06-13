/**
 * @file sys_fault_dump.c
 * @brief 故障事件环实现（.noinit retained RAM）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            Phase 2 初始版本
 *
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/sys_fault_dump.h>

LOG_MODULE_REGISTER(sys_fault_dump, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义
 * ============================================================================= */

#define FAULT_DUMP_MAGIC   0x5A4C4446U
#define FAULT_DUMP_PAYLOAD CONFIG_SYS_FAULT_DUMP_PAYLOAD_MAX

typedef struct {
    uint32_t kind;
    uint32_t ts_ms;
    uint16_t len;
    uint8_t  data[FAULT_DUMP_PAYLOAD];
} fault_dump_entry_t;

typedef struct {
    uint32_t           magic;
    uint16_t           head;
    uint16_t           count;
    fault_dump_entry_t entries[CONFIG_SYS_FAULT_DUMP_RING_SIZE];
} fault_dump_ring_t;

/* =============================================================================
 * 静态变量（软复位后保留，硬复位/上电清除）
 * ============================================================================= */

static fault_dump_ring_t g_fault_ring __noinit;
static bool              g_fault_ready;

/* =============================================================================
 * 内部辅助
 * ============================================================================= */

static void fault_dump_reset_ring(void) {
    memset(&g_fault_ring, 0, sizeof(g_fault_ring));
    g_fault_ring.magic = FAULT_DUMP_MAGIC;
    g_fault_ring.head = 0U;
    g_fault_ring.count = 0U;
}

/* =============================================================================
 * API
 * ============================================================================= */

int sys_fault_dump_init(void) {
    if (g_fault_ready) {
        return 0;
    }

    if (g_fault_ring.magic != FAULT_DUMP_MAGIC) {
        fault_dump_reset_ring();
    }

    g_fault_ready = true;
    LOG_INF("sys_fault_dump ready (ring=%u)", (unsigned int) CONFIG_SYS_FAULT_DUMP_RING_SIZE);
    return 0;
}

int sys_fault_dump_record(fault_dump_kind_t kind, const void* data, size_t len) {
    fault_dump_entry_t* entry;
    uint16_t            idx;

    if (!g_fault_ready) {
        (void) sys_fault_dump_init();
    }

    if (kind == 0U) {
        return -EINVAL;
    }

    if (len > FAULT_DUMP_PAYLOAD) {
        len = FAULT_DUMP_PAYLOAD;
    }

    idx = g_fault_ring.head;
    entry = &g_fault_ring.entries[idx];

    entry->kind = (uint32_t) kind;
    entry->ts_ms = k_uptime_get_32();
    entry->len = (uint16_t) len;
    memset(entry->data, 0, sizeof(entry->data));
    if (data != NULL && len > 0U) {
        memcpy(entry->data, data, len);
    }

    g_fault_ring.head = (uint16_t) ((idx + 1U) % (uint16_t) CONFIG_SYS_FAULT_DUMP_RING_SIZE);
    if (g_fault_ring.count < (uint16_t) CONFIG_SYS_FAULT_DUMP_RING_SIZE) {
        g_fault_ring.count++;
    }

    LOG_DBG("fault dump kind=%u len=%u", (unsigned int) kind, (unsigned int) len);
    return 0;
}

int sys_fault_dump_export(uint8_t* out, size_t out_len, size_t* out_written) {
    size_t need;

    if (!g_fault_ready || out == NULL || out_len == 0U) {
        return -EINVAL;
    }

    need = sizeof(g_fault_ring);
    if (out_len < need) {
        return -ENOMEM;
    }

    memcpy(out, &g_fault_ring, need);
    if (out_written != NULL) {
        *out_written = need;
    }
    return 0;
}

void sys_fault_dump_clear(void) {
    fault_dump_reset_ring();
}

/* =============================================================================
 * SYS_INIT
 * ============================================================================= */

static int sys_fault_dump_auto_init(void) {
    return sys_fault_dump_init();
}

SYS_INIT(sys_fault_dump_auto_init, POST_KERNEL, APP_INIT_PRIO_SYS_FAULT_DUMP);
