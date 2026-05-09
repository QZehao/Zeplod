# Thread IPC Service 模块详细使用说明

## 目录

- [概述](#概述)
- [与 Zephyr 上游 IPC Service 的区别](#与-zephyr-上游-ipc-service-的区别)
- [架构与数据流](#架构与数据流)
- [配置选项](#配置选项)
- [编译与集成](#编译与集成)
- [与事件系统联动](#与事件系统联动)
- [与模块管理器结合](#与模块管理器结合)
- [核心类型](#核心类型)
- [API 参考](#api-参考)
- [三种调用模式](#三种调用模式)
- [服务生命周期与重复初始化](#服务生命周期与重复初始化)
- [内存、并发与安全](#内存并发与安全)
- [最佳实践](#最佳实践)
- [故障排除](#故障排除)
- [示例代码说明](#示例代码说明)

**模块开发（module_manager）**：另见 **[Thread_IPC模块集成指南.md](34-Thread_IPC模块集成指南.md)**。

---

## 概述

**Thread IPC Service**（本仓库路径 `src/modules/ipc_service/`）是在 **单核应用进程内** 实现的「请求—响应」式服务框架：

- 一个 **Worker 线程**从请求队列取出消息，调用你注册的 **`ipc_service_func_t`** 处理业务，并把结果放入响应队列。
- 一个 **Dispatcher 线程**从响应队列取出结果，根据 `request_id` 匹配挂起的调用，完成 **同步唤醒**、**异步回调** 或 **Future 信号量释放**。

支持的三种客户端调用方式：

| 模式 | 行为概要 |
|------|----------|
| **Sync** | 阻塞直到结果返回或超时（`k_sem_take`） |
| **Async** | 立即返回，结果在 Dispatcher 线程中调用回调 |
| **Future** | 返回 `ipc_future_t *`，可 `ipc_future_wait` 或轮询 `ipc_future_is_ready` |

### 主要特性

| 特性 | 说明 |
|------|------|
| 无内核堆依赖 | 队列缓冲与双线程栈均嵌入 `ipc_service_t`，不调用 `k_malloc` / `k_calloc` |
| Kconfig 总开关 | `CONFIG_THREAD_IPC_SERVICE` |
| 可选示例 | `CONFIG_THREAD_IPC_SERVICE_EXAMPLE` |
| 可选事件桥 | `CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE`：将处理结果发布到工程自带 **事件系统** |
| 线程模型 | Worker + Dispatcher，二者优先级相同（由 `CONFIG_THREAD_IPC_SERVICE_PRIORITY` 配置） |

---

## 与 Zephyr 上游 IPC Service 的区别

Zephyr 在 **Inter Processor Communication** 菜单下有 **`CONFIG_IPC_SERVICE`**，用于 **多核 / RPMsg / icmsg 等后端** 的 IPC Service 子系统。

本模块故意使用 **不同的 Kconfig 符号**：

- 本模块：`THREAD_IPC_SERVICE` → `CONFIG_THREAD_IPC_SERVICE`
- 上游：`IPC_SERVICE` → `CONFIG_IPC_SERVICE`

二者 **不要混用名称**。在 `prj.conf` 中应使用 `CONFIG_THREAD_IPC_SERVICE=y` 启用本模块；正常情况下 **不应** 同时为了本模块去打开 `CONFIG_IPC_SERVICE`（除非你真的需要 Zephyr 的多核 IPC Service）。

头文件 `ipc_service.h` 在 **`CONFIG_THREAD_IPC_SERVICE` 未开启** 时会触发编译期 `#error`，避免误包含。

---

## 架构与数据流

```text
  调用线程                    Worker 线程                 Dispatcher 线程
      |                            |                            |
      | ipc_call_*                 |                            |
      |--------------------------->|  k_msgq(request_queue)     |
      | 登记 pending + 投递请求     |  service_func()            |
      |                            |--------------------------->|
      |                            |  k_msgq(response_queue)    | 匹配 request_id
      |<---------------------------|                            | Sync: k_sem_give
      |  Sync: sem 唤醒            |                            | Async: callback()
      |  Async: 回调在 Dispatcher  |                            | Future: future->sem
```

要点：

- **请求 ID** 由 `ipc_generate_request_id()` 生成，**0 为保留值**（内部不会分配 0）。
- **异步回调**在 **Dispatcher 上下文**执行，且 **在释放 `pending_lock` 之后**调用，避免与同步路径死锁。
- **停止服务**时向两个队列各投递一条「哑」消息，两个线程在取出消息后检查 `shutdown` 并退出，随后 `ipc_service_stop()` 对两个线程执行 `k_thread_join`。

---

## 配置选项

在 **menuconfig** 中路径：**Thread IPC service (in-app)**（根 `Kconfig` 通过 `rsource "src/modules/ipc_service/Kconfig"` 引入）。

**全项目配置项集中说明**（含本节各宏的 `CONFIG_*` 释义与依赖）：见 **[项目配置项说明.md](../40-应用开发/42-项目配置项说明.md) §4**。

| Kconfig 符号 | 类型 | 默认值 | 含义 |
|--------------|------|--------|------|
| `THREAD_IPC_SERVICE` | bool | n | 总开关 → `CONFIG_THREAD_IPC_SERVICE` |
| `THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS` | int | 8 | 最大并发未完成请求数（2–64） |
| `THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE` | int | 4 | 请求消息队列深度（2–32） |
| `THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE` | int | 4 | 响应消息队列深度（2–32） |
| `THREAD_IPC_SERVICE_STACK_SIZE` | int | 1024 | Worker / Dispatcher **各自**栈大小（字节，512–8192） |
| `THREAD_IPC_SERVICE_PRIORITY` | int | 5 | 两线程优先级（-16–15） |
| `THREAD_IPC_SERVICE_LOG_LEVEL` | int | 1 | 模块日志级别（0–4），生产环境建议 0 或 1 |
| `THREAD_IPC_SERVICE_EXAMPLE` | bool | n | 是否编译内置示例 |
| `THREAD_IPC_SERVICE_EVENT_BRIDGE` | bool | n | 编译 `ipc_service_event.c`：向事件总线发布 IPC 结果（依赖 `EVENT_SYSTEM`） |
| `EXAMPLE_MODULE_THREAD_IPC` | bool | n | 编译 `example_module_ipc` 并与 `module_manager` 集成（依赖 `THREAD_IPC_SERVICE` + `MODULE_MANAGER`） |

`prj.conf` 示例（默认配置，适合 64KB SRAM；**事件桥 / 事件系统依赖 `k_malloc`，须保留非零堆**）：

```conf
CONFIG_THREAD_IPC_SERVICE=y
CONFIG_THREAD_IPC_SERVICE_EXAMPLE=n
CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE=y
CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS=8
CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE=4
CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE=4
CONFIG_THREAD_IPC_SERVICE_STACK_SIZE=1024
CONFIG_THREAD_IPC_SERVICE_PRIORITY=5
CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL=1
```

**20KB SRAM 极小配置示例**（总占用约 2KB）：

```conf
CONFIG_THREAD_IPC_SERVICE=y
CONFIG_THREAD_IPC_SERVICE_STACK_SIZE=512
CONFIG_THREAD_IPC_SERVICE_PRIORITY=6
CONFIG_THREAD_IPC_SERVICE_LOG_LEVEL=0
CONFIG_THREAD_IPC_SERVICE_MAX_PENDING_REQUESTS=4
CONFIG_THREAD_IPC_SERVICE_REQUEST_QUEUE_SIZE=2
CONFIG_THREAD_IPC_SERVICE_RESPONSE_QUEUE_SIZE=2
CONFIG_THREAD_IPC_SERVICE_SHARED_MEM=y
CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE=2
CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE=128
CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_LOG_LEVEL=0
CONFIG_THREAD_IPC_SERVICE_EXAMPLE=n
```

**重要：** `ipc_service_t` 内部的缓冲区与栈大小由上述宏 **在编译期固定**。`ipc_service_init()` 只需传入 `service`、`name`、`service_func`、`priority` 参数，栈大小和队列大小由 Kconfig 配置决定。

---

## 编译与集成

### CMake

根目录 `CMakeLists.txt` 已按 Kconfig 条件加入源码与头路径：

- `CONFIG_THREAD_IPC_SERVICE`：编译 `ipc_service.c`，并增加 `src/modules/ipc_service` 到 include 路径。
- `CONFIG_THREAD_IPC_SERVICE_EXAMPLE`：额外编译 `ipc_service_example.c`。
- `CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE`：额外编译 `ipc_service_event.c`（需同时启用事件系统 `CONFIG_EVENT_SYSTEM`）。

自定义应用时，在 `prj.conf` 打开 `CONFIG_THREAD_IPC_SERVICE=y` 后，使用：

```c
#include "ipc_service.h"
```

若关闭模块，请勿包含该头文件（会 `#error`），并确保工程中无对已剥离符号的引用。

### 可选叠加配置（打开示例）

仓库提供 `prj_ipc_example.conf`，仅含：

```conf
CONFIG_THREAD_IPC_SERVICE_EXAMPLE=y
```

构建时合并（注意 PowerShell 下引号与路径）：

```text
west build -b <board> -- '-DEXTRA_CONF_FILE=D:/path/to/zephyr_template/prj_ipc_example.conf'
```

仓库还提供 **`prj_ipc_event_bridge.conf`**（仅打开 `CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE`），可与基础 `prj.conf` 合并用于验证「IPC → 事件」编译与链接。

---

## 与事件系统联动

本工程 **可以** 在业务里把 Thread IPC Service 与 **`event_system`（发布/订阅）** 结合使用，典型用途是：IPC 在 Worker 中处理完请求后，把 **结果元数据** 广播给多个订阅模块，而不仅限于单次 `ipc_call_*` 的调用方。

### 关系说明

| 机制 | 角色 |
|------|------|
| Thread IPC | 点对点请求—响应（带 `request_id`、同步/异步/Future） |
| Event System | 一对多通知（订阅者各自在事件分发线程中收回调） |

二者互补：IPC 适合 **命令式 RPC**；事件适合 **状态/结果广播** 与 **解耦模块**。

### 方式一：可选事件桥（推荐，已实现）

在 `prj.conf` 中增加：

```conf
CONFIG_EVENT_SYSTEM=y
CONFIG_THREAD_IPC_SERVICE=y
CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE=y
```

在 **`event_system_init()` 之后**、发布任何事件之前，调用一次（可重复调用，类型注册幂等）：

```c
#include "ipc_service_event.h"

thread_ipc_event_register_types();
```

在 **`event_system_start()` 之后** 再在 `ipc_service_func_t` 内发布（`event_publish_copy` 要求调度器已运行，否则事件会被丢弃）：

```c
static int my_ipc_service_func(ipc_request_id_t request_id,
			       const void *data, size_t data_size,
			       void **out_data, size_t *out_data_size)
{
	int ret = /* ... 业务处理 ... */;

	(void)thread_ipc_event_publish_result(
		MY_SOURCE_ID,   /* 例如模块 ID，便于订阅方过滤 */
		request_id,
		ret,
		EVENT_PRIORITY_NORMAL);

	*out_data = /* ... */;
	*out_data_size = /* ... */;
	return ret;
}
```

订阅方使用已有 API：

```c
#include "event_system.h"

static void on_thread_ipc_result(const event_t *ev, void *user_data)
{
	const thread_ipc_event_result_t *p =
		(const thread_ipc_event_result_t *)ev->data;

	(void)user_data;
	/* 根据 p->source_id / p->request_id / p->result 处理 */
}

/* 初始化阶段：event_subscribe(EVENT_TYPE_THREAD_IPC_RESPONSE, on_thread_ipc_result, NULL, &sub_id); */
```

事件类型常量在 **`event_system.h`** 中定义为 `EVENT_TYPE_THREAD_IPC_RESPONSE`（值为 `20`），载荷结构体为 **`thread_ipc_event_result_t`**（定义在 `ipc_service_event.h`）。

**注意：**

- 桥接函数只携带 **固定小结构**（`source_id`、`request_id`、`result`）。若需附带大块数据，请在 `service_func` 内自行调用 `event_publish_copy` / `event_create_with_data` 使用 **单独的事件类型**，并自行保证 `data` 生命周期至分发线程消费完毕。
- 事件系统内部会使用堆分配事件体（与「IPC 模块本体无堆」不矛盾）。

### 方式二：不开启 Kconfig，在 service_func 内直接发事件

若不想增加 `ipc_service_event.c`，可在 `ipc_service_func_t` 中直接 `#include "event_system.h"` 并调用 `event_publish_copy` 等，同样需在 **`event_system_start()` 之后** 再发布。

---

## 与模块管理器结合

**可以** 把「持有一个 `ipc_service_t` 的模块」注册进 **`module_manager`**，在虚表 `init` / `start` / `stop` 中驱动 IPC 生命周期，与事件订阅一起在 `init` 里完成。

### 推荐顺序（示意）

```text
event_system_init()
  → thread_ipc_event_register_types()   （若启用 EVENT_BRIDGE）
  → module_manager_init() / 各模块 init（ipc_service_init）
event_system_start()
  → module_manager_start()（ipc_service_start）
  → … 运行业务 …
module_manager_stop()（ipc_service_stop）
```

### `module_interface_t` 与 IPC 的对应关系（概念）

| 模块回调 | IPC 操作 |
|----------|----------|
| `init` | `ipc_service_init()`；可在此 `event_subscribe` |
| `start` | `ipc_service_start()` |
| `stop` | `ipc_service_stop()` |
| 再次启动前 | 需再次 `ipc_service_init()`（见前文「重复初始化」） |

具体字段名以 **`module_base.h`** 中 `module_interface_t` 为准；本仓库的 `example_module_a/b` 可作为注册方式的参考，将其中业务替换为 `ipc_service_*` 即可。

---

## 核心类型

### 服务处理函数

```c
typedef int (*ipc_service_func_t)(ipc_request_id_t request_id,
                                  const void *data,
                                  size_t data_size,
                                  void **out_data,
                                  size_t *out_data_size);
```

- 在 **Worker 线程**中调用。
- 返回值：成功建议 `0`，失败为 **负的 errno 风格**（如 `-EINVAL`）；该值会传给客户端（Sync 的返回值、Async 回调的 `result`、Future 的 `future->result`）。
- `*out_data` / `*out_data_size`：输出缓冲区指针与长度；若无需输出，可将 `*out_data` 置 `NULL` 且 `*out_data_size` 置 `0`。

### 异步回调

```c
typedef void (*ipc_async_callback_t)(ipc_request_id_t request_id,
                                     int result,
                                     const void *data,
                                     size_t data_size,
                                     void *user_data);
```

在 **Dispatcher 线程**调用，**不在**持有 `pending_lock` 的情况下调用。

### 服务实例 `ipc_service_t`

包含：`k_msgq`、内嵌队列缓冲、`K_KERNEL_STACK_MEMBER` 定义的 **worker_stack** 与 **dispatcher_stack**、`pending_requests[]`、`futures[]`、互斥锁等。实例体积较大，适合作为 **静态全局** 或 **长时间存活** 的上下文成员；注意 RAM 占用（尤其 `THREAD_IPC_SERVICE_STACK_SIZE` 翻倍）。

---

## API 参考

### 生命周期

| 函数 | 说明 |
|------|------|
| `int ipc_service_init(ipc_service_t *service, const char *name, ipc_service_func_t service_func, size_t stack_size, int priority, size_t request_queue_size, size_t response_queue_size)` | 初始化结构体、消息队列、互斥锁、Future 空闲链表。参数中的三项大小 **须与 Kconfig 一致**。 |
| `int ipc_service_start(ipc_service_t *service)` | 创建并启动 Worker 与 Dispatcher 线程。已运行则 `-EALREADY`。 |
| `int ipc_service_stop(ipc_service_t *service)` | 置 `shutdown`、唤醒两线程、`k_thread_join`。**不释放**内嵌缓冲（随实例生命周期）。未启动则直接返回 `0`。 |

### 调用接口

| 函数 | 说明 |
|------|------|
| `int ipc_call_sync(..., k_timeout_t timeout)` | 同步调用；`data` 不可为 `NULL`（即使 `data_size` 为 0）；`out_data` / `out_data_size` 不可为 `NULL`。 |
| `int ipc_call_async(..., ipc_async_callback_t callback, void *user_data, ipc_request_id_t *out_request_id)` | 异步调用；`callback` 不可为 `NULL`；`out_request_id` 可为 `NULL`。 |
| `int ipc_call_future(..., ipc_future_t **out_future)` | 分配 Future 并投递请求；成功后须 `ipc_future_release`。 |

### Future

| 函数 | 说明 |
|------|------|
| `int ipc_future_wait(ipc_future_t *future, int *out_result, const void **out_data, size_t *out_data_size, k_timeout_t timeout)` | 等待完成信号量；输出参数均可为 `NULL`（按需）。 |
| `bool ipc_future_is_ready(ipc_future_t *future)` | 非阻塞查询 `completed` 标志。 |
| `int ipc_future_release(ipc_service_t *service, ipc_future_t *future)` | 归还 Future 到池；会校验指针是否属于该 `service` 的 `futures[]`，否则 `-EINVAL`。 |

### 工具

| 函数 | 说明 |
|------|------|
| `size_t ipc_service_get_pending_count(ipc_service_t *service)` | 当前 `in_use` 的 pending 条目数（加锁统计）。 |
| `int ipc_service_cancel(ipc_service_t *service, ipc_request_id_t request_id)` | 取消仍挂在 pending 表中的请求；未找到则 `-ENOENT`。 |
| `ipc_request_id_t ipc_generate_request_id(void)` | 全局递增 ID（跳过 0）；多实例共享同一计数器。 |

### 事件桥（`CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE`）

| 函数 | 说明 |
|------|------|
| `event_status_t thread_ipc_event_register_types(void)` | 注册 `EVENT_TYPE_THREAD_IPC_RESPONSE`（幂等）。须在 `event_system_init()` 之后调用。 |
| `event_status_t thread_ipc_event_publish_result(uint32_t source_id, ipc_request_id_t request_id, int result, event_priority_t priority)` | `event_publish_copy` 固定载荷；须在 `event_system_start()` 之后、`service_func` 等线程上下文中调用。 |

---

## 三种调用模式

### 1. Sync（同步）

典型流程：

1. 调用 `ipc_call_sync`，内部登记 pending、投递请求、在 `response_sem` 上阻塞。
2. Worker 处理完后由 Dispatcher `k_sem_give` 唤醒调用线程。
3. 调用线程读取 `out_data` / `out_data_size` 与函数返回值（即 `service_func` 的 result）。

超时或失败时 pending 会被释放；若请求已发出但超时，仍可能出现「晚到的响应」找不到 pending（会打警告日志），属预期行为。

### 2. Async（异步）

- 调用立即返回 `0`（仅表示 **入队成功**，不代表业务成功）。
- 业务完成后在 **Dispatcher** 线程执行 `callback`。
- **不要在回调里长时间阻塞**；若回调内再次调用同一服务的同步接口，需注意优先级与死锁风险。

### 3. Future

- `ipc_call_future` 成功后得到 `ipc_future_t *`，可先轮询 `ipc_future_is_ready`，再 `ipc_future_wait`（或只 wait）。
- 用毕必须 `ipc_future_release`，否则泄漏 Future 槽位。

---

## 服务生命周期与重复初始化

- **启动顺序**：`ipc_service_init` → `ipc_service_start`。
- **停止**：`ipc_service_stop` 会 join 两线程并将 `running` 置为 false；**不会** `memset` 整个 `ipc_service_t`。
- **再次使用**：若需重新启动服务，应再次调用 **`ipc_service_init`**（内部会 `memset` 整个实例并重建队列与状态），再 `ipc_service_start`。不要在未 `init` 的情况下对已 `stop` 的实例直接 `start`。

---

## 内存、并发与安全

1. **输入缓冲区生命周期**：`service_func` 收到的 `data` 指向调用方传入的内存，必须保证从 **入队到 Worker 返回前** 有效；同理，`out_data` 若指向调用方栈或临时缓冲，须保证客户端在 **读完结果前** 仍然有效。
2. **信任边界**：`service_func` 与异步回调均在 **内核线程**中运行，应视为与应用程序同一信任域；不要执行不可信数据驱动的危险操作。
3. **`ipc_service_cancel`**：
   - **Sync**：设置 `result = -ECANCELED` 并 `k_sem_give`，pending 由 `ipc_call_sync` 路径释放。
   - **Future**：设置 future 结果并 `k_sem_give`，并释放 pending。
   - **Async**：仅释放 pending；若响应稍后到达，可能打出「无 pending」警告，响应被丢弃。
4. **ISR**：当前 API 使用 `k_mutex` 与 `k_sem_take`，**不应在中断上下文中**直接调用 `ipc_call_sync` / `ipc_call_async` / `ipc_call_future`（除非 Zephyr 版本对具体 API 有明确 ISR 安全说明；本模块按线程使用设计）。

---

## 最佳实践

- 队列深度与 `MAX_PENDING_REQUESTS` 按峰值并发设置；队列满时 `k_msgq_put(..., K_FOREVER)` 会阻塞调用线程。
- Worker 内 **`service_func` 应尽快完成**；重计算建议投递到工作队列或专用线程，避免堵塞整条服务链。
- 日志模块名注册为 **`thread_ipc_svc`**，调试时可在日志过滤中按模块名查看。
- RAM 紧张时：减小 `THREAD_IPC_SERVICE_STACK_SIZE`、队列深度与 pending 数量；注意 `ipc_service_t` 含 **两个** 同尺寸栈，总栈开销约为 `2 * STACK_SIZE` 加上队列与 pending 结构。

---

## 故障排除

| 现象 | 可能原因 | 处理方向 |
|------|----------|----------|
| 编译提示 `ipc_service.h requires CONFIG_THREAD_IPC_SERVICE` | 未开 Kconfig 却包含头文件 | `prj.conf` 增加 `CONFIG_THREAD_IPC_SERVICE=y` 或移除 include |
| `ipc_service_init` 返回 `-EINVAL` | `stack_size` / 队列参数与 Kconfig 不一致 | 使用与 `prj.conf` 相同的数值，或先改 Kconfig 再改代码 |
| 同步调用永久阻塞 | 未 `start`、Worker 未运行、或 `service_func` 死锁 | 确认 `ipc_service_start`；检查 Worker 栈与函数实现 |
| `ipc_call_async` 无回调 | Dispatcher 未运行或 pending 丢失 | 确认服务已启动；查看是否有 cancel / 超时释放 pending |
| `ipc_future_release` 返回 `-EINVAL` | `future` 不是该 `service` 分配 | 检查指针来源与多实例混用 |
| 链接或配置出现 Zephyr `IPC_SERVICE` 相关后端 | 误开 `CONFIG_IPC_SERVICE` | 本模块只用 `CONFIG_THREAD_IPC_SERVICE` |
| 链接 `undefined reference to k_malloc` | `CONFIG_HEAP_MEM_POOL_SIZE=0` | 在 `prj.conf` 中设置 **`CONFIG_HEAP_MEM_POOL_SIZE`**（如 8192）；事件系统需要内核堆 |
| `region RAM overflowed` | 静态栈/队列过大或多演示同时开 | 减小 `THREAD_IPC_SERVICE_*`、勿同时开 **`EXAMPLE`** 与 **`EXAMPLE_MODULE_THREAD_IPC`**，或换更大 SRAM 板卡 |

---

## 示例代码说明

文件：`src/modules/ipc_service/ipc_service_example.c`（由 `CONFIG_THREAD_IPC_SERVICE_EXAMPLE=y` 编译）。

- 在 `SYS_INIT` 中 `ipc_service_init` + `ipc_service_start`，并创建一个演示线程依次跑 Sync / Async / Future。
- 示例中的 `example_service_func` 将 `*out_data` 指回输入 `data`（零拷贝演示），实际项目中常改为写入静态缓冲或调用方约定的输出区。

可将该文件作为模板复制到自己的模块中，并删除或替换 `SYS_INIT` 注册方式，改为由应用主流程显式初始化。

---

## 版本与维护

- 实现文件：`ipc_service.c` / `ipc_service.h`；可选 `ipc_service_event.c` / `ipc_service_event.h`
- 事件类型常量：`EVENT_TYPE_THREAD_IPC_RESPONSE`（`src/core/event_system.h`）
- Kconfig：`src/modules/ipc_service/Kconfig`
- 本文档应与上述文件保持同步；若 API 或 Kconfig 变更，请同步更新本节与表格中的符号名。
