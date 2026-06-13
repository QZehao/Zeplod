/**
 * @file ota_transport.h
 * @brief OTA 传输层抽象（可插拔 vtable）
 *
 * Phase 1 提供 null 传输（RAM 模拟镜像，供 native_sim 测试）；
 * Phase 2 可接入 MCUboot / img_mgmt 等后端。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 */

#ifndef OTA_TRANSPORT_H
#define OTA_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 传输层 vtable
 * ============================================================================= */

typedef struct ota_transport_ops ota_transport_ops_t;

/**
 * @brief OTA 传输操作表
 *
 * 各回调在 ota_module 调用线程上下文中执行（Phase 1 为调用者线程同步调用）。
 */
struct ota_transport_ops {
    void* ctx;
    int (*open)(ota_transport_ops_t* ops);
    int (*write_chunk)(ota_transport_ops_t* ops, size_t offset, const uint8_t* data, size_t len);
    int (*verify)(ota_transport_ops_t* ops);
    int (*close)(ota_transport_ops_t* ops);
    void (*abort)(ota_transport_ops_t* ops);
};

/* =============================================================================
 * 内置传输后端
 * ============================================================================= */

/**
 * @brief 获取 null 传输实例（RAM 模拟镜像）
 * @return 静态 vtable 指针，生命周期为整个进程
 */
const ota_transport_ops_t* ota_transport_null_get(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_TRANSPORT_H */
