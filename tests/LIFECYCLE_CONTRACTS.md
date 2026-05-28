# 核心模块生命周期与并发边界契约

> 与 [84-核心架构重构路线图](../zh-CN/80-贡献与维护/84-核心架构重构路线图.md) P0 对齐。  
> 日期：2026-05-28

本文档固化 **测试应断言的返回值语义**；实现变更须先更新本文档与对应用例。

## event_system

| 场景 | 预期 |
| --- | --- |
| 未 `init` 调用 `start` / `publish` | `EVENT_ERR_INVALID_ARG` 或 `EVENT_ERR_NOT_RUNNING`（见具体 API） |
| 未 `start` 时 `publish` | `EVENT_ERR_NOT_RUNNING` |
| 重复 `init` | `EVENT_OK`（幂等） |
| 重复 `start`（已 RUNNING） | `EVENT_OK` |
| 重复 `stop` | `EVENT_OK` |
| `STOPPED` 后再次 `start` | `EVENT_OK`，`is_running()==true` |
| 重复 `shutdown` | `EVENT_OK` |
| 从 dispatcher 线程 `stop` / `shutdown` | `EVENT_ERR_INVALID_ARG` |
| 订阅未注册类型 | `EVENT_ERR_NOT_FOUND` |
| 空回调订阅 | `EVENT_ERR_INVALID_ARG` |

## event_dispatcher

| 场景 | 预期 |
| --- | --- |
| 未 `init` 时 `start` | `EVENT_ERR_INVALID_ARG` |
| 重复 `start`（已 RUNNING） | `EVENT_OK` |
| 重复 `stop` | `EVENT_OK` |
| `ever_started` 后手动 `process_one` | `EVENT_ERR_INVALID_ARG` |
| 从 dispatcher 线程 `stop` / `deinit` | `EVENT_ERR_INVALID_ARG` |
| join 超时 | `EVENT_ERR_TIMEOUT`（线程可能仍存活，见实现注释） |

## ipc_service

| 场景 | 预期 |
| --- | --- |
| 未 `start` 时 `ipc_call_sync` | 非 0 失败 |
| 重复 `start` / `stop` | 0（幂等） |
| `cancel` 后 `stop` 交叠 | 见 `test_cancel_*` 用例 |

## 测试编写约定

- 异步完成优先使用 `ztest_sync.h` 中的 `ztest_wait_atomic_*` 或 `k_sem` / `k_event`。
- 禁止将 `(event_callback_t)0x1000` 等不可调用地址作为有效订阅回调。
- 生命周期循环测试至少覆盖：`init → start → stop → (可选再次 start) → shutdown`。
