/**
 * @file sys_secure_kv.c
 * @brief 安全 KV 服务实现
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
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zeplod/app_config.h>
#include <zeplod/lock_order.h>
#include <zeplod/sys_secure_kv.h>

LOG_MODULE_REGISTER(sys_secure_kv, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部定义
 * ============================================================================= */

#define SECURE_KV_KEY_MAX CONFIG_SYS_SECURE_KV_KEY_MAX_LEN
#define SECURE_KV_VAL_MAX CONFIG_SYS_SECURE_KV_VALUE_MAX_LEN
#define SECURE_KV_MAX     CONFIG_SYS_SECURE_KV_MAX_ENTRIES
#define SECURE_KEY_BYTES  16U

/* =============================================================================
 * 内部数据结构
 * ============================================================================= */

typedef struct {
    char    key[SECURE_KV_KEY_MAX];
    uint8_t cipher[SECURE_KV_VAL_MAX];
    size_t  cipher_len;
    bool    in_use;
} secure_kv_slot_t;

typedef struct {
    secure_kv_slot_t slots[SECURE_KV_MAX];
    uint8_t          key[SECURE_KEY_BYTES];
    struct k_mutex   lock;
    bool             ready;
} sys_secure_kv_cb_t;

/* =============================================================================
 * 静态变量
 * ============================================================================= */

static sys_secure_kv_cb_t g_secure_kv;

/* =============================================================================
 * 前置声明
 * ============================================================================= */

static uint32_t secure_kv_mix(uint32_t h, uint8_t b);
static uint8_t  secure_kv_keystream_byte(const uint8_t key[SECURE_KEY_BYTES], uint8_t slot_id, size_t idx);
static void     secure_kv_crypt_buffer(uint8_t* buf, size_t len, const uint8_t key[SECURE_KEY_BYTES],
                                       uint8_t slot_id);
static int      secure_kv_parse_key_hex(const char* hex, uint8_t out[SECURE_KEY_BYTES]);
static int      secure_kv_find_locked(const char* key);
static int      secure_kv_find_free_locked(void);
static void     secure_kv_strncpy(char* dst, const char* src, size_t cap);

/* =============================================================================
 * 内部辅助（Phase 2 软件 keystream，可替换为 PSA）
 * ============================================================================= */

static void secure_kv_strncpy(char* dst, const char* src, size_t cap) {
    if (cap == 0U) {
        return;
    }
    (void) strncpy(dst, src, cap - 1U);
    dst[cap - 1U] = '\0';
}

static void secure_kv_lock(void) {
    zepl_lock_enter(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_secure_kv.lock);
    k_mutex_lock(&g_secure_kv.lock, K_FOREVER);
}

static void secure_kv_unlock(void) {
    k_mutex_unlock(&g_secure_kv.lock);
    zepl_lock_exit(ZEP_LOCK_LEVEL_RESOURCE, (uintptr_t) &g_secure_kv.lock);
}

static uint32_t secure_kv_mix(uint32_t h, uint8_t b) {
    h ^= (uint32_t) b;
    h *= 0x01000193U;
    return h;
}

static uint8_t secure_kv_keystream_byte(const uint8_t key[SECURE_KEY_BYTES], uint8_t slot_id, size_t idx) {
    uint32_t h = 0x811c9dc5U ^ ((uint32_t) slot_id << 16) ^ (uint32_t) idx;

    for (size_t i = 0U; i < SECURE_KEY_BYTES; i++) {
        h = secure_kv_mix(h, key[i]);
    }
    return (uint8_t) (h ^ (h >> 8));
}

static void secure_kv_crypt_buffer(uint8_t* buf, size_t len, const uint8_t key[SECURE_KEY_BYTES], uint8_t slot_id) {
    for (size_t i = 0U; i < len; i++) {
        buf[i] ^= secure_kv_keystream_byte(key, slot_id, i);
    }
}

static int secure_kv_parse_key_hex(const char* hex, uint8_t out[SECURE_KEY_BYTES]) {
    if (hex == NULL || strlen(hex) != (SECURE_KEY_BYTES * 2U)) {
        return -EINVAL;
    }

    for (size_t i = 0U; i < SECURE_KEY_BYTES; i++) {
        unsigned int byte;

        if (sscanf(&hex[i * 2U], "%2x", &byte) != 1) {
            return -EINVAL;
        }
        out[i] = (uint8_t) byte;
    }
    return 0;
}

static int secure_kv_find_locked(const char* key) {
    for (int i = 0; i < SECURE_KV_MAX; i++) {
        if (g_secure_kv.slots[i].in_use && strcmp(g_secure_kv.slots[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int secure_kv_find_free_locked(void) {
    for (int i = 0; i < SECURE_KV_MAX; i++) {
        if (!g_secure_kv.slots[i].in_use) {
            return i;
        }
    }
    return -1;
}

/* =============================================================================
 * 核心 API
 * ============================================================================= */

int sys_secure_kv_init(void) {
    int ret;

    if (g_secure_kv.ready) {
        return 0;
    }

    k_mutex_init(&g_secure_kv.lock);
    memset(g_secure_kv.slots, 0, sizeof(g_secure_kv.slots));

    ret = secure_kv_parse_key_hex(CONFIG_SYS_SECURE_KV_KEY_HEX, g_secure_kv.key);
    if (ret != 0) {
        LOG_ERR("Invalid SYS_SECURE_KV_KEY_HEX");
        return ret;
    }

    g_secure_kv.ready = true;
    LOG_INF("sys_secure_kv ready");
    return 0;
}

int sys_secure_kv_set(const char* key, const uint8_t* plain, size_t plain_len) {
    int  slot;
    char key_copy[SECURE_KV_KEY_MAX];

    if (!g_secure_kv.ready || key == NULL || plain == NULL || plain_len == 0U) {
        return -EINVAL;
    }
    if (plain_len > SECURE_KV_VAL_MAX) {
        return -ENOMEM;
    }
    if (strlen(key) >= SECURE_KV_KEY_MAX) {
        return -EINVAL;
    }

    secure_kv_strncpy(key_copy, key, sizeof(key_copy));

    secure_kv_lock();

    slot = secure_kv_find_locked(key_copy);
    if (slot < 0) {
        slot = secure_kv_find_free_locked();
        if (slot < 0) {
            secure_kv_unlock();
            return -ENOMEM;
        }
        secure_kv_strncpy(g_secure_kv.slots[slot].key, key_copy, sizeof(g_secure_kv.slots[slot].key));
        g_secure_kv.slots[slot].in_use = true;
    }

    memcpy(g_secure_kv.slots[slot].cipher, plain, plain_len);
    secure_kv_crypt_buffer(g_secure_kv.slots[slot].cipher, plain_len, g_secure_kv.key, (uint8_t) slot);
    g_secure_kv.slots[slot].cipher_len = plain_len;

    secure_kv_unlock();
    return 0;
}

int sys_secure_kv_get(const char* key, uint8_t* out, size_t out_len, size_t* out_written) {
    int    slot;
    size_t n;

    if (!g_secure_kv.ready || key == NULL || out == NULL || out_len == 0U) {
        return -EINVAL;
    }

    secure_kv_lock();

    slot = secure_kv_find_locked(key);
    if (slot < 0) {
        secure_kv_unlock();
        return -ENOENT;
    }

    n = g_secure_kv.slots[slot].cipher_len;
    if (n > out_len) {
        secure_kv_unlock();
        return -ENOMEM;
    }

    memcpy(out, g_secure_kv.slots[slot].cipher, n);
    secure_kv_crypt_buffer(out, n, g_secure_kv.key, (uint8_t) slot);

    secure_kv_unlock();

    if (out_written != NULL) {
        *out_written = n;
    }
    return 0;
}

int sys_secure_kv_remove(const char* key) {
    int slot;

    if (!g_secure_kv.ready || key == NULL) {
        return -EINVAL;
    }

    secure_kv_lock();

    slot = secure_kv_find_locked(key);
    if (slot < 0) {
        secure_kv_unlock();
        return -ENOENT;
    }

    memset(&g_secure_kv.slots[slot], 0, sizeof(g_secure_kv.slots[slot]));
    secure_kv_unlock();
    return 0;
}

bool sys_secure_kv_has(const char* key) {
    bool found = false;

    if (!g_secure_kv.ready || key == NULL) {
        return false;
    }

    secure_kv_lock();
    found = (secure_kv_find_locked(key) >= 0);
    secure_kv_unlock();
    return found;
}

void sys_secure_kv_clear(void) {
    if (!g_secure_kv.ready) {
        return;
    }

    secure_kv_lock();
    memset(g_secure_kv.slots, 0, sizeof(g_secure_kv.slots));
    secure_kv_unlock();
}

/* =============================================================================
 * SYS_INIT
 * ============================================================================= */

static int sys_secure_kv_auto_init(void) {
    return sys_secure_kv_init();
}

SYS_INIT(sys_secure_kv_auto_init, POST_KERNEL, APP_INIT_PRIO_SYS_SECURE_KV);
