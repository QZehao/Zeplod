/**
 * @file app_kv.c
 * @brief 应用键值表实现
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "app_kv.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#if APP_CONFIG_ENABLE_APP_KV && IS_ENABLED(CONFIG_APP_KV_PERSIST)
#include <zephyr/settings/settings.h>
#endif

#if APP_CONFIG_ENABLE_APP_KV

typedef struct {
    char  key[APP_KV_KEY_MAX_LEN];
    char  value[APP_KV_VALUE_MAX_LEN];
    bool  in_use;
} app_kv_slot_t;

static struct k_mutex s_kv_lock;
static app_kv_slot_t   s_kv[APP_KV_MAX_ENTRIES];
static bool            s_kv_ready;

static int find_key_locked(const char* key) {
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (s_kv[i].in_use && strcmp(s_kv[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_locked(void) {
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (!s_kv[i].in_use) {
            return i;
        }
    }
    return -1;
}

#if IS_ENABLED(CONFIG_APP_KV_PERSIST)

#define KV_PERSIST_MAGIC   0x01764B41u
#define KV_PERSIST_VERSION 1U
#define KV_SETTINGS_KEY    "app_kv/d"

static int kv_decode_into_slots(const uint8_t* buf, size_t len) {
    if (len < 8U) {
        return APP_ERR_INVALID_PARAM;
    }
    uint32_t magic = sys_get_le32(buf);
    if (magic != KV_PERSIST_MAGIC) {
        return APP_ERR_INVALID_PARAM;
    }
    uint16_t ver = sys_get_le16(buf + 4U);
    if (ver != KV_PERSIST_VERSION) {
        return APP_ERR_INVALID_PARAM;
    }
    uint16_t count = sys_get_le16(buf + 6U);
    if (count > (uint16_t) APP_KV_MAX_ENTRIES) {
        return APP_ERR_INVALID_PARAM;
    }

    memset(s_kv, 0, sizeof(s_kv));
    size_t off = 8U;

    for (uint16_t n = 0U; n < count; n++) {
        if (off + 2U > len) {
            return APP_ERR_INVALID_PARAM;
        }
        uint8_t klen = buf[off++];
        uint8_t vlen = buf[off++];
        if (klen >= APP_KV_KEY_MAX_LEN || vlen >= APP_KV_VALUE_MAX_LEN) {
            return APP_ERR_INVALID_PARAM;
        }
        if (off + (size_t) klen + 1U + (size_t) vlen + 1U > len) {
            return APP_ERR_INVALID_PARAM;
        }

        int idx = find_free_locked();
        if (idx < 0) {
            return APP_ERR_KV_FULL;
        }

        memcpy(s_kv[idx].key, buf + off, (size_t) klen + 1U);
        off += (size_t) klen + 1U;
        memcpy(s_kv[idx].value, buf + off, (size_t) vlen + 1U);
        off += (size_t) vlen + 1U;

        if (s_kv[idx].key[klen] != '\0' || s_kv[idx].value[vlen] != '\0') {
            return APP_ERR_INVALID_PARAM;
        }
        s_kv[idx].in_use = true;
    }

    if (off != len) {
        return APP_ERR_INVALID_PARAM;
    }
    return APP_OK;
}

static int kv_encode_blob_locked(uint8_t* buf, size_t cap, size_t* out_len) {
    if (cap < 8U) {
        return APP_ERR_MEMORY;
    }

    uint16_t nused = 0U;
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (s_kv[i].in_use) {
            nused++;
        }
    }

    size_t off = 0U;
    sys_put_le32(KV_PERSIST_MAGIC, buf + off);
    off += 4U;
    sys_put_le16(KV_PERSIST_VERSION, buf + off);
    off += 2U;
    sys_put_le16(nused, buf + off);
    off += 2U;

    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (!s_kv[i].in_use) {
            continue;
        }
        size_t klen = strlen(s_kv[i].key);
        size_t vlen = strlen(s_kv[i].value);
        if (klen >= (size_t) APP_KV_KEY_MAX_LEN || vlen >= (size_t) APP_KV_VALUE_MAX_LEN) {
            return APP_ERR_INVALID_PARAM;
        }
        if (off + 2U + klen + 1U + vlen + 1U > cap) {
            return APP_ERR_MEMORY;
        }
        buf[off++] = (uint8_t) klen;
        buf[off++] = (uint8_t) vlen;
        memcpy(buf + off, s_kv[i].key, klen + 1U);
        off += klen + 1U;
        memcpy(buf + off, s_kv[i].value, vlen + 1U);
        off += vlen + 1U;
    }

    *out_len = off;
    return APP_OK;
}

static void kv_autosave_if_enabled(void) {
#if IS_ENABLED(CONFIG_APP_KV_PERSIST_AUTOSAVE)
    (void) app_kv_save();
#endif
}

#endif /* CONFIG_APP_KV_PERSIST */

void app_kv_init(void) {
    if (s_kv_ready) {
        return;
    }
    k_mutex_init(&s_kv_lock);
    memset(s_kv, 0, sizeof(s_kv));

#if IS_ENABLED(CONFIG_APP_KV_PERSIST)
    if (settings_subsys_init() == 0) {
        uint8_t blob[APP_KV_PERSIST_BLOB_MAX];
        ssize_t n = settings_load_one(KV_SETTINGS_KEY, blob, sizeof(blob));
        if (n > 0) {
            k_mutex_lock(&s_kv_lock, K_FOREVER);
            int d = kv_decode_into_slots(blob, (size_t) n);
            k_mutex_unlock(&s_kv_lock);
            if (d != APP_OK) {
                k_mutex_lock(&s_kv_lock, K_FOREVER);
                memset(s_kv, 0, sizeof(s_kv));
                k_mutex_unlock(&s_kv_lock);
            }
        }
    }
#endif

    s_kv_ready = true;
}

int app_kv_set(const char* key, const char* value) {
    if (!s_kv_ready || key == NULL || value == NULL) {
        return APP_ERR_INVALID_PARAM;
    }
    if (key[0] == '\0') {
        return APP_ERR_INVALID_PARAM;
    }
    if (strlen(key) >= APP_KV_KEY_MAX_LEN || strlen(value) >= APP_KV_VALUE_MAX_LEN) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&s_kv_lock, K_FOREVER);

    int idx = find_key_locked(key);
    if (idx < 0) {
        idx = find_free_locked();
        if (idx < 0) {
            k_mutex_unlock(&s_kv_lock);
            return APP_ERR_KV_FULL;
        }
    }

    (void) strncpy(s_kv[idx].key, key, APP_KV_KEY_MAX_LEN - 1);
    s_kv[idx].key[APP_KV_KEY_MAX_LEN - 1] = '\0';
    (void) strncpy(s_kv[idx].value, value, APP_KV_VALUE_MAX_LEN - 1);
    s_kv[idx].value[APP_KV_VALUE_MAX_LEN - 1] = '\0';
    s_kv[idx].in_use = true;

    k_mutex_unlock(&s_kv_lock);
#if IS_ENABLED(CONFIG_APP_KV_PERSIST)
    kv_autosave_if_enabled();
#endif
    return APP_OK;
}

int app_kv_get(const char* key, char* out, size_t out_len) {
    if (!s_kv_ready || key == NULL || out == NULL || out_len == 0U) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&s_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    if (idx < 0) {
        k_mutex_unlock(&s_kv_lock);
        return APP_ERR_NOT_FOUND;
    }
    (void) strncpy(out, s_kv[idx].value, out_len - 1U);
    out[out_len - 1U] = '\0';
    k_mutex_unlock(&s_kv_lock);
    return APP_OK;
}

bool app_kv_has(const char* key) {
    if (!s_kv_ready || key == NULL) {
        return false;
    }
    k_mutex_lock(&s_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    k_mutex_unlock(&s_kv_lock);
    return idx >= 0;
}

int app_kv_remove(const char* key) {
    if (!s_kv_ready || key == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&s_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    if (idx < 0) {
        k_mutex_unlock(&s_kv_lock);
        return APP_ERR_NOT_FOUND;
    }
    memset(&s_kv[idx], 0, sizeof(s_kv[idx]));
    k_mutex_unlock(&s_kv_lock);
#if IS_ENABLED(CONFIG_APP_KV_PERSIST)
    kv_autosave_if_enabled();
#endif
    return APP_OK;
}

void app_kv_clear(void) {
    if (!s_kv_ready) {
        return;
    }
    k_mutex_lock(&s_kv_lock, K_FOREVER);
    memset(s_kv, 0, sizeof(s_kv));
    k_mutex_unlock(&s_kv_lock);
#if IS_ENABLED(CONFIG_APP_KV_PERSIST)
    kv_autosave_if_enabled();
#endif
}

size_t app_kv_count(void) {
    if (!s_kv_ready) {
        return 0U;
    }
    size_t n = 0U;
    k_mutex_lock(&s_kv_lock, K_FOREVER);
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (s_kv[i].in_use) {
            n++;
        }
    }
    k_mutex_unlock(&s_kv_lock);
    return n;
}

int app_kv_foreach(app_kv_visit_fn fn, void* user) {
    if (!s_kv_ready || fn == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&s_kv_lock, K_FOREVER);
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (!s_kv[i].in_use) {
            continue;
        }
        int r = fn(s_kv[i].key, s_kv[i].value, user);
        if (r != 0) {
            k_mutex_unlock(&s_kv_lock);
            return r;
        }
    }
    k_mutex_unlock(&s_kv_lock);
    return 0;
}

int app_kv_set_int32(const char* key, int32_t v) {
    char buf[16];
    (void) snprintf(buf, sizeof(buf), "%ld", (long) v);
    return app_kv_set(key, buf);
}

int app_kv_get_int32(const char* key, int32_t* out) {
    if (out == NULL) {
        return APP_ERR_INVALID_PARAM;
    }
    char buf[APP_KV_VALUE_MAX_LEN];
    int  ret = app_kv_get(key, buf, sizeof(buf));
    if (ret != APP_OK) {
        return ret;
    }
    char* end = NULL;
    long  lv  = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') {
        return APP_ERR_INVALID_PARAM;
    }
    *out = (int32_t) lv;
    return APP_OK;
}

int app_kv_save(void) {
#if !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    return APP_ERR_DISABLED;
#else
    if (!s_kv_ready) {
        return APP_ERR_INIT;
    }
    uint8_t  blob[APP_KV_PERSIST_BLOB_MAX];
    size_t   len = 0U;
    k_mutex_lock(&s_kv_lock, K_FOREVER);
    int enc = kv_encode_blob_locked(blob, sizeof(blob), &len);
    k_mutex_unlock(&s_kv_lock);
    if (enc != APP_OK) {
        return enc;
    }
    if (settings_subsys_init() != 0) {
        return APP_ERR_INIT;
    }
    int w = settings_save_one(KV_SETTINGS_KEY, blob, len);
    return w == 0 ? APP_OK : APP_ERR_IO;
#endif
}

int app_kv_load(void) {
#if !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    return APP_ERR_DISABLED;
#else
    if (!s_kv_ready) {
        return APP_ERR_INIT;
    }
    if (settings_subsys_init() != 0) {
        return APP_ERR_INIT;
    }
    uint8_t blob[APP_KV_PERSIST_BLOB_MAX];
    ssize_t n = settings_load_one(KV_SETTINGS_KEY, blob, sizeof(blob));
    if (n < 0) {
        if (n == -ENOENT) {
            k_mutex_lock(&s_kv_lock, K_FOREVER);
            memset(s_kv, 0, sizeof(s_kv));
            k_mutex_unlock(&s_kv_lock);
            return APP_OK;
        }
        return APP_ERR_IO;
    }
    k_mutex_lock(&s_kv_lock, K_FOREVER);
    int d = kv_decode_into_slots(blob, (size_t) n);
    if (d != APP_OK) {
        memset(s_kv, 0, sizeof(s_kv));
    }
    k_mutex_unlock(&s_kv_lock);
    return d == APP_OK ? APP_OK : APP_ERR_INVALID_PARAM;
#endif
}

#else /* !APP_CONFIG_ENABLE_APP_KV */

void app_kv_init(void) {}

int app_kv_set(const char* key, const char* value) {
    ARG_UNUSED(key);
    ARG_UNUSED(value);
    return APP_ERR_DISABLED;
}

int app_kv_get(const char* key, char* out, size_t out_len) {
    ARG_UNUSED(key);
    ARG_UNUSED(out);
    ARG_UNUSED(out_len);
    return APP_ERR_DISABLED;
}

bool app_kv_has(const char* key) {
    ARG_UNUSED(key);
    return false;
}

int app_kv_remove(const char* key) {
    ARG_UNUSED(key);
    return APP_ERR_DISABLED;
}

void app_kv_clear(void) {}

size_t app_kv_count(void) {
    return 0U;
}

int app_kv_foreach(app_kv_visit_fn fn, void* user) {
    ARG_UNUSED(fn);
    ARG_UNUSED(user);
    return APP_ERR_DISABLED;
}

int app_kv_set_int32(const char* key, int32_t v) {
    ARG_UNUSED(key);
    ARG_UNUSED(v);
    return APP_ERR_DISABLED;
}

int app_kv_get_int32(const char* key, int32_t* out) {
    ARG_UNUSED(key);
    ARG_UNUSED(out);
    return APP_ERR_DISABLED;
}

int app_kv_save(void) {
    return APP_ERR_DISABLED;
}

int app_kv_load(void) {
    return APP_ERR_DISABLED;
}

#endif /* APP_CONFIG_ENABLE_APP_KV */
