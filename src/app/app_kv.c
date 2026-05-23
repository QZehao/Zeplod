/**
 * @file app_kv.c
 * @brief 应用键值表实现
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
 */

#include "app_kv.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app_kv, CONFIG_SYS_LOG_LEVEL);

#if APP_CONFIG_ENABLE_APP_KV && IS_ENABLED(CONFIG_APP_KV_PERSIST)
#include <zephyr/settings/settings.h>
#endif

#if APP_CONFIG_ENABLE_APP_KV

typedef struct {
    char key[APP_KV_KEY_MAX_LEN];
    char value[APP_KV_VALUE_MAX_LEN];
    bool in_use;
} app_kv_slot_t;

static struct k_mutex g_kv_lock;
static app_kv_slot_t  g_kv[APP_KV_MAX_ENTRIES];
static bool           g_kv_ready;

static int find_key_locked(const char* key) {
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (g_kv[i].in_use && strcmp(g_kv[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static void kv_strncpy_fill(char* dst, const char* src, size_t cap) {
    if (cap == 0U) {
        return;
    }
    (void) strncpy(dst, src, cap - 1U);
    dst[cap - 1U] = '\0';
}

#if IS_ENABLED(CONFIG_APP_KV_PERSIST)

static int find_free_locked(void) {
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (!g_kv[i].in_use) {
            return i;
        }
    }
    return -1;
}

#define KV_PERSIST_MAGIC   0x01764B41u
#define KV_PERSIST_VERSION 1U
#define KV_SETTINGS_KEY    "app_kv/d"

/** 持久化编解码缓冲（静态，避免 SYS_INIT/shell 栈上分配 APP_KV_PERSIST_BLOB_MAX） */
static uint8_t g_kv_persist_blob[APP_KV_PERSIST_BLOB_MAX];

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

    memset(g_kv, 0, sizeof(g_kv));
    size_t off = 8U;

    for (uint16_t n = 0U; n < count; n++) {
        if (off + 2U > len) {
            return APP_ERR_INVALID_PARAM;
        }
        uint8_t klen = buf[off++];
        uint8_t vlen = buf[off++];
        if (klen == 0U || klen >= APP_KV_KEY_MAX_LEN || vlen >= APP_KV_VALUE_MAX_LEN) {
            return APP_ERR_INVALID_PARAM;
        }
        if (off + (size_t) klen + 1U + (size_t) vlen + 1U > len) {
            return APP_ERR_INVALID_PARAM;
        }

        char tmp_key[APP_KV_KEY_MAX_LEN];
        char tmp_val[APP_KV_VALUE_MAX_LEN];

        memcpy(tmp_key, buf + off, (size_t) klen + 1U);
        off += (size_t) klen + 1U;
        memcpy(tmp_val, buf + off, (size_t) vlen + 1U);
        off += (size_t) vlen + 1U;

        if (tmp_key[klen] != '\0' || tmp_val[vlen] != '\0') {
            return APP_ERR_INVALID_PARAM;
        }

        int idx = find_key_locked(tmp_key);
        if (idx < 0) {
            idx = find_free_locked();
            if (idx < 0) {
                return APP_ERR_KV_FULL;
            }
            memcpy(g_kv[idx].key, tmp_key, sizeof(tmp_key));
        }
        memcpy(g_kv[idx].value, tmp_val, sizeof(tmp_val));
        g_kv[idx].in_use = true;
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
        if (g_kv[i].in_use) {
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
        if (!g_kv[i].in_use) {
            continue;
        }
        size_t klen = strlen(g_kv[i].key);
        size_t vlen = strlen(g_kv[i].value);
        if (klen == 0U || klen >= (size_t) APP_KV_KEY_MAX_LEN || vlen >= (size_t) APP_KV_VALUE_MAX_LEN) {
            return APP_ERR_INVALID_PARAM;
        }
        if (off + 2U + klen + 1U + vlen + 1U > cap) {
            return APP_ERR_MEMORY;
        }
        buf[off++] = (uint8_t) klen;
        buf[off++] = (uint8_t) vlen;
        memcpy(buf + off, g_kv[i].key, klen + 1U);
        off += klen + 1U;
        memcpy(buf + off, g_kv[i].value, vlen + 1U);
        off += vlen + 1U;
    }

    *out_len = off;
    return APP_OK;
}

#endif /* CONFIG_APP_KV_PERSIST */

#if IS_ENABLED(CONFIG_APP_KV_PERSIST) && IS_ENABLED(CONFIG_APP_KV_PERSIST_AUTOSAVE)
#define KV_AUTOSAVE() (void) app_kv_save()
#else
#define KV_AUTOSAVE() ((void) 0)
#endif

void app_kv_init(void) {
    if (g_kv_ready) {
        return;
    }
    k_mutex_init(&g_kv_lock);
    memset(g_kv, 0, sizeof(g_kv));

#if IS_ENABLED(CONFIG_APP_KV_PERSIST)
    if (settings_subsys_init() != 0) {
        LOG_WRN("settings_subsys_init failed; app_kv not loaded from flash");
    } else {
        k_mutex_lock(&g_kv_lock, K_FOREVER);
        ssize_t n = settings_load_one(KV_SETTINGS_KEY, g_kv_persist_blob, sizeof(g_kv_persist_blob));
        if (n < 0 && n != -ENOENT) {
            LOG_WRN("settings_load_one(%s) failed: %zd", KV_SETTINGS_KEY, n);
        } else if (n > 0) {
            int d = kv_decode_into_slots(g_kv_persist_blob, (size_t) n);
            if (d != APP_OK) {
                LOG_WRN("app_kv flash blob invalid or corrupt (err=%d), cleared RAM table", d);
                memset(g_kv, 0, sizeof(g_kv));
            }
        }
        k_mutex_unlock(&g_kv_lock);
    }
#endif

    g_kv_ready = true;
}

int app_kv_set(const char* key, const char* value) {
    if (!g_kv_ready || key == NULL || value == NULL) {
        return APP_ERR_INVALID_PARAM;
    }
    if (key[0] == '\0') {
        return APP_ERR_INVALID_PARAM;
    }
    if (strlen(key) >= APP_KV_KEY_MAX_LEN || strlen(value) >= APP_KV_VALUE_MAX_LEN) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);

    int idx = -1;
    int free_idx = -1;
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (g_kv[i].in_use) {
            if (strcmp(g_kv[i].key, key) == 0) {
                idx = i;
                break;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }
    if (idx < 0) {
        if (free_idx < 0) {
            k_mutex_unlock(&g_kv_lock);
            return APP_ERR_KV_FULL;
        }
        idx = free_idx;
    }

    kv_strncpy_fill(g_kv[idx].key, key, APP_KV_KEY_MAX_LEN);
    kv_strncpy_fill(g_kv[idx].value, value, APP_KV_VALUE_MAX_LEN);
    g_kv[idx].in_use = true;

    k_mutex_unlock(&g_kv_lock);
    KV_AUTOSAVE();
    return APP_OK;
}

int app_kv_get(const char* key, char* out, size_t out_len) {
    if (!g_kv_ready || key == NULL || out == NULL || out_len == 0U) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    if (idx < 0) {
        k_mutex_unlock(&g_kv_lock);
        return APP_ERR_NOT_FOUND;
    }
    kv_strncpy_fill(out, g_kv[idx].value, out_len);
    k_mutex_unlock(&g_kv_lock);
    return APP_OK;
}

bool app_kv_has(const char* key) {
    if (!g_kv_ready || key == NULL) {
        return false;
    }
    k_mutex_lock(&g_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    k_mutex_unlock(&g_kv_lock);
    return idx >= 0;
}

int app_kv_remove(const char* key) {
    if (!g_kv_ready || key == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);
    int idx = find_key_locked(key);
    if (idx < 0) {
        k_mutex_unlock(&g_kv_lock);
        return APP_ERR_NOT_FOUND;
    }
    memset(&g_kv[idx], 0, sizeof(g_kv[idx]));
    k_mutex_unlock(&g_kv_lock);
    KV_AUTOSAVE();
    return APP_OK;
}

void app_kv_clear(void) {
    if (!g_kv_ready) {
        return;
    }
    k_mutex_lock(&g_kv_lock, K_FOREVER);
    memset(g_kv, 0, sizeof(g_kv));
    k_mutex_unlock(&g_kv_lock);
    KV_AUTOSAVE();
}

size_t app_kv_count(void) {
    if (!g_kv_ready) {
        return 0U;
    }
    size_t n = 0U;
    k_mutex_lock(&g_kv_lock, K_FOREVER);
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (g_kv[i].in_use) {
            n++;
        }
    }
    k_mutex_unlock(&g_kv_lock);
    return n;
}

int app_kv_foreach(app_kv_visit_fn fn, void* user) {
    if (!g_kv_ready || fn == NULL) {
        return APP_ERR_INVALID_PARAM;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);
    for (int i = 0; i < APP_KV_MAX_ENTRIES; i++) {
        if (!g_kv[i].in_use) {
            continue;
        }
        int r = fn(g_kv[i].key, g_kv[i].value, user);
        if (r != 0) {
            k_mutex_unlock(&g_kv_lock);
            return r;
        }
    }
    k_mutex_unlock(&g_kv_lock);
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
    errno = 0;
    char* end = NULL;
    long  lv = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') {
        return APP_ERR_INVALID_PARAM;
    }
    if (errno == ERANGE || lv < (long) INT32_MIN || lv > (long) INT32_MAX) {
        return APP_ERR_INVALID_PARAM;
    }
    *out = (int32_t) lv;
    return APP_OK;
}

int app_kv_save(void) {
#if !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    return APP_ERR_DISABLED;
#else
    if (!g_kv_ready) {
        return APP_ERR_INIT;
    }
    size_t len = 0U;
    k_mutex_lock(&g_kv_lock, K_FOREVER);
    int enc = kv_encode_blob_locked(g_kv_persist_blob, sizeof(g_kv_persist_blob), &len);
    k_mutex_unlock(&g_kv_lock);
    if (enc != APP_OK) {
        return enc;
    }
    if (settings_subsys_init() != 0) {
        return APP_ERR_INIT;
    }
    int w = settings_save_one(KV_SETTINGS_KEY, g_kv_persist_blob, len);
    return w == 0 ? APP_OK : APP_ERR_IO;
#endif
}

int app_kv_load(void) {
#if !IS_ENABLED(CONFIG_APP_KV_PERSIST)
    return APP_ERR_DISABLED;
#else
    if (!g_kv_ready) {
        return APP_ERR_INIT;
    }
    if (settings_subsys_init() != 0) {
        return APP_ERR_INIT;
    }
    k_mutex_lock(&g_kv_lock, K_FOREVER);
    ssize_t n = settings_load_one(KV_SETTINGS_KEY, g_kv_persist_blob, sizeof(g_kv_persist_blob));
    if (n < 0) {
        k_mutex_unlock(&g_kv_lock);
        if (n == -ENOENT) {
            k_mutex_lock(&g_kv_lock, K_FOREVER);
            memset(g_kv, 0, sizeof(g_kv));
            k_mutex_unlock(&g_kv_lock);
            return APP_OK;
        }
        return APP_ERR_IO;
    }
    int d = kv_decode_into_slots(g_kv_persist_blob, (size_t) n);
    if (d != APP_OK) {
        memset(g_kv, 0, sizeof(g_kv));
    }
    k_mutex_unlock(&g_kv_lock);
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
