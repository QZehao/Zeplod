/**
 * @file zepl_thread_service.h
 * @brief 线程服务生命周期共享常量与约定（P2）
 *
 * 不封装具体 stop/join 实现；各服务保留现有 API。
 * 语义对照见 docs/zh-CN/80-贡献与维护/85-线程服务生命周期约定.md
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-05-28
 */

#ifndef ZEPL_THREAD_SERVICE_H
#define ZEPL_THREAD_SERVICE_H

#include <stdint.h>

/** 默认线程 join 超时（毫秒），与 event_dispatcher / ipc_service 对齐 */
#ifndef ZEPL_THREAD_SERVICE_JOIN_TIMEOUT_MS
#define ZEPL_THREAD_SERVICE_JOIN_TIMEOUT_MS 500U
#endif

/** 轮询式线程循环中 msgq/阻塞点的默认切片超时（毫秒） */
#ifndef ZEPL_THREAD_SERVICE_POLL_TIMEOUT_MS
#define ZEPL_THREAD_SERVICE_POLL_TIMEOUT_MS 100U
#endif

#endif /* ZEPL_THREAD_SERVICE_H */
