/**
 * @file app_kv.h
 * @brief 应用键值配置（字符串 key / value，默认 RAM）
 *
 * 可选 CONFIG_APP_KV_PERSIST：整表序列化为单条 Settings blob 写入 flash（需分区与 overlay）。
 * 线程安全。
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

#ifndef APP_KV_H
#define APP_KV_H

#include <zeplod/app_config.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化键值表与互斥锁（由 SYS_INIT 调用；可重复调用，无副作用）
 */
void app_kv_init(void);

/**
 * @brief 写入或覆盖键值（拷贝字符串，key/value 须含结尾 0）
 * @return APP_OK，或 APP_ERR_* / APP_ERR_KV_FULL
 */
int app_kv_set(const char* key, const char* value);

/**
 * @brief 读取键对应值，写入 out（含 NUL，至多 out_len-1 字符）
 * @return APP_OK；未找到为 APP_ERR_NOT_FOUND。value 长于 out_len-1 时仍会截断并返回
 * APP_OK（调用方可用更长缓冲或先估长）
 */
int app_kv_get(const char* key, char* out, size_t out_len);

/**
 * @brief 是否存在键
 */
bool app_kv_has(const char* key);

/**
 * @brief 删除键
 * @return APP_OK；不存在为 APP_ERR_NOT_FOUND
 */
int app_kv_remove(const char* key);

/** 清空全部条目 */
void app_kv_clear(void);

/** 当前占用槽位数 */
size_t app_kv_count(void);

typedef int (*app_kv_visit_fn)(const char* key, const char* value, void* user);

/**
 * @brief 遍历有效键值；回调返回非 0 时中止并返回该值
 * @note 回调内勿再调用会抢占同一把互斥锁的 app_kv_*（如 set/get/remove/clear/save/load/foreach），以免死锁
 */
int app_kv_foreach(app_kv_visit_fn fn, void* user);

int app_kv_set_int32(const char* key, int32_t v);
int app_kv_get_int32(const char* key, int32_t* out);

/**
 * @brief 将当前 RAM 表写入 flash（需 CONFIG_APP_KV_PERSIST）
 * @return APP_OK；未编译持久化或未启用 Kconfig 时为 APP_ERR_DISABLED；失败为 APP_ERR_IO 等
 */
int app_kv_save(void);

/**
 * @brief 从 flash 重新加载并替换当前 RAM 表（需 CONFIG_APP_KV_PERSIST）
 */
int app_kv_load(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_KV_H */
