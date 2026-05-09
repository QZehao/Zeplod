# IPC 服务扩展特性规划

本文档记录 IPC 服务框架的扩展特性规划，供后续开发参考。

---

## 已实现特性

### ✅ 零拷贝缓冲区 (ipc_buffer_t)

**状态：** 已实现

**功能：**
- 引用计数实现安全跨线程共享
- 池化静态分配，无动态内存
- 外部内存包装（零拷贝从调用者）
- 清确所有权语义（ref/unref）

**使用示例：**
```c
// 分配缓冲区
ipc_buffer_t* buf = ipc_buffer_alloc(&pool, 256);
ipc_buffer_set_size(buf, memcpy(ipc_buffer_get_data(buf), data, len));

// 共享缓冲区
ipc_buffer_ref(buf);
send_to_other_thread(buf);

// 释放
ipc_buffer_unref(buf);
```

**配置选项：**
- `CONFIG_THREAD_IPC_BUFFER` - 启用特性
- `CONFIG_THREAD_IPC_BUFFER_SMALL_SIZE/COUNT` - 小缓冲区池
- `CONFIG_THREAD_IPC_BUFFER_MEDIUM_SIZE/COUNT` - 中缓冲区池
- `CONFIG_THREAD_IPC_BUFFER_LARGE_SIZE/COUNT` - 大缓冲区池

---

## 待引入特性

### 1. 中间件/拦截器链 (Priority: ⭐⭐⭐)

**价值：** 高扩展性、易测试、横切关注点分离

**设计概述：**
```c
typedef int (*ipc_middleware_t)(ipc_service_t* service,
                                 ipc_request_id_t rid,
                                 const void* data, size_t size,
                                 void** out_data, size_t* out_size,
                                 ipc_middleware_t next);

// 使用示例
ipc_service_use(&service, logging_middleware);
ipc_service_use(&service, validation_middleware);
ipc_service_use(&service, timeout_middleware);
```

**应用场景：**
- 日志记录
- 请求验证
- 超时控制
- 指标收集
- 权限检查

**实现要点：**
- 中间件链表管理
- 调用顺序保证
- 错误传播机制

---

### 2. 熔断器模式 (Priority: ⭐⭐⭐)

**价值：** 提高系统韧性，防止级联故障

**设计概述：**
```c
typedef struct ipc_circuit_breaker {
    atomic_t      failure_count;
    atomic_t      state;           // CLOSED, OPEN, HALF_OPEN
    k_timeout_t   open_timeout;
    int           failure_threshold;
} ipc_circuit_breaker_t;

// 状态转换
// CLOSED (正常) --失败达阈值--> OPEN (熔断)
// OPEN --超时--> HALF_OPEN (试探)
// HALF_OPEN --成功--> CLOSED / --失败--> OPEN
```

**配置选项：**
- `CONFIG_THREAD_IPC_CIRCUIT_BREAKER`
- `CONFIG_THREAD_IPC_CB_FAILURE_THRESHOLD` - 触发熔断的失败次数
- `CONFIG_THREAD_IPC_CB_OPEN_TIMEOUT_MS` - 熔断持续时间

---

### 3. 指标收集 (Priority: ⭐⭐⭐)

**价值：** 可观测性基础，性能监控

**设计概述：**
```c
typedef struct ipc_metrics {
    atomic_t total_requests;
    atomic_t total_successes;
    atomic_t total_failures;
    atomic_t total_timeouts;
    int64_t  total_latency_ns;
    int64_t  max_latency_ns;
} ipc_metrics_t;

// API
int ipc_service_get_metrics(ipc_service_t* service, ipc_metrics_t* out);
```

**收集指标：**
- 请求总数/成功/失败/超时
- 平均/最大/最小延迟
- 当前 pending 数量
- 队列使用率

**集成方式：**
- 通过中间件自动收集
- 或在核心代码中埋点

---

### 4. 重试策略 (Priority: ⭐⭐)

**价值：** 提高可靠性，处理瞬态故障

**设计概述：**
```c
typedef struct ipc_retry_policy {
    int           max_retries;
    k_timeout_t   initial_delay;
    float         backoff_factor;  // 指数退避因子
    k_timeout_t   max_delay;
    bool          jitter;          // 添加抖动避免惊群
} ipc_retry_policy_t;

// API
int ipc_call_with_retry(ipc_service_t* service,
                        const void* data, size_t size,
                        ipc_retry_policy_t* policy,
                        void** out_data, size_t* out_size);
```

**重试策略：**
- 固定间隔
- 指数退避
- 指数退避 + 抖动

---

### 5. 健康检查 (Priority: ⭐⭐)

**价值：** 运维友好，服务状态监控

**设计概述：**
```c
typedef struct ipc_health_check {
    int64_t    last_healthy_time;
    uint32_t   consecutive_errors;
    atomic_t   is_healthy;
} ipc_health_check_t;

// API
bool ipc_service_is_healthy(ipc_service_t* service);
int ipc_service_health_check(ipc_service_t* service);
```

**实现方式：**
- 内置心跳请求（PING/PONG）
- 定期健康检查线程
- 连续错误计数

---

### 6. 无锁 Pending 表 (Priority: ⭐)

**价值：** 极致性能，消除锁争用

**设计概述：**
```c
// 无锁哈希表
typedef struct ipc_pending_slot {
    ipc_request_id_t request_id;
    atomic_uintptr_t data;
} ipc_pending_slot_t;

// 使用原子 CAS 操作
ipc_pending_request_t* find_pending_lockless(ipc_service_t* service,
                                              ipc_request_id_t rid);
```

**适用场景：**
- 高并发场景（每秒数千请求）
- 实时性要求高的系统

**实现复杂度：** 高

---

### 7. 服务注册与发现 (Priority: ⭐)

**价值：** 动态服务发现，松耦合

**设计概述：**
```c
// 服务注册表
int ipc_registry_register(const char* name, ipc_service_t* service);
ipc_service_t* ipc_registry_lookup(const char* name);

// 便捷调用
int ipc_call_by_name(const char* service_name,
                     const void* data, size_t size,
                     void** out, size_t* out_size,
                     k_timeout_t timeout);
```

**适用场景：**
- 插件式架构
- 运行时服务绑定
- 配置驱动的服务选择

---

### 8. 链路追踪 (Priority: ⭐)

**价值：** 分布式调试，请求链路可视化

**设计概述：**
```c
typedef struct ipc_trace_context {
    uint64_t trace_id;
    uint64_t span_id;
    uint64_t parent_span_id;
    uint32_t flags;
} ipc_trace_context_t;

// 在请求消息中传递
typedef struct ipc_request_msg {
    // ...
    ipc_trace_context_t trace_ctx;
} ipc_request_msg_t;
```

**集成方式：**
- 与 OpenTelemetry 集成
- 自定义追踪后端

---

### 9. 事件总线集成 (Priority: ⭐)

**价值：** 事件驱动架构，解耦订阅

**设计概述：**
```c
// IPC 结果作为事件发布
typedef struct ipc_event {
    uint32_t          event_type;
    ipc_request_id_t  request_id;
    int               result;
    const void*       data;
    size_t            data_size;
    const char*       service_name;
} ipc_event_t;

// 订阅
int ipc_subscribe(const char* service_name,
                  ipc_event_handler_t handler,
                  void* user_data);
```

**当前状态：** 已有 `CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE` 基础实现

---

### 10. RPC 代码生成 (Priority: ⭐)

**价值：** 开发效率，类型安全

**设计概述：**
```yaml
# 服务定义文件 (YAML)
service: SensorService
methods:
  - name: read_temperature
    input: TemperatureRequest
    output: TemperatureResponse
```

生成代码：
```c
// 自动生成
int sensor_read_temperature(float* value, k_timeout_t timeout);
```

---

## 特性优先级矩阵

| 特性 | 价值 | 复杂度 | 优先级 |
|------|------|--------|--------|
| 零拷贝缓冲区 | 高 | 中 | ✅ 已实现 |
| 中间件/拦截器 | 高 | 中 | ⭐⭐⭐ |
| 熔断器 | 高 | 中 | ⭐⭐⭐ |
| 指标收集 | 高 | 低 | ⭐⭐⭐ |
| 重试策略 | 中 | 低 | ⭐⭐ |
| 健康检查 | 中 | 低 | ⭐⭐ |
| 无锁 Pending 表 | 中 | 高 | ⭐ |
| 服务注册表 | 中 | 中 | ⭐ |
| 链路追踪 | 中 | 高 | ⭐ |
| 事件总线集成 | 低 | 中 | ⭐ |
| RPC 代码生成 | 低 | 高 | ⭐ |

---

## 设计原则

1. **可选性** - 所有扩展特性通过 Kconfig 配置，默认禁用
2. **向后兼容** - 新 API 不破坏现有代码
3. **零开销** - 未使用的特性不增加代码大小或运行时开销
4. **静态分配** - 保持无动态内存的设计原则
5. **线程安全** - 所有 API 在多线程环境下安全使用

---

## 贡献指南

如果您想实现某个特性，请：

1. 在 GitHub Issues 中讨论设计方案
2. 参考现有代码风格和架构
3. 添加相应的 Kconfig 选项
4. 编写单元测试
5. 更新文档

---

*文档版本: 1.0*
*最后更新: 2026-04-13*
