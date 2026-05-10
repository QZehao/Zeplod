> Language: [中文](../../zh-CN/30-核心模块/35-IPC服务扩展特性规划.md) | **English**

# IPC Service Expansion Feature Planning

This document records expansion feature planning for the IPC service framework, for subsequent development reference.

---

## Implemented Features

### ✅ Shared Memory Pool (ipc_shm_*)

**Status**: Implemented (replaces original `ipc_buffer_t` planning)

**Functionality**:
- Reference-counted fixed-size block pool (slab style)
- Each block carries reference count, lifecycle explicitly controlled by alloc/acquire/release
- Statically allocated, no runtime k_malloc
- Calls `ipc_call_sync_shm` to return handle, service passes handle back to caller in response

**Differences from original `ipc_buffer_t` planning**:
- No longer uses separate "buffer" object, but slab-style fixed-size block pool
- Each block carries reference count, lifecycle explicitly controlled by alloc/acquire/release
- Calls `ipc_call_sync_shm` to return handle, service passes handle back to caller in response

**Configuration Options**:
- `CONFIG_THREAD_IPC_SERVICE_SHARED_MEM` - Enable shared memory subsystem
- `CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_POOL_SIZE` - Memory block pool size
- `CONFIG_THREAD_IPC_SERVICE_SHARED_MEM_BLOCK_SIZE` - Single block size

For detailed API, see **[Thread IPC Service Usage Guide.md](33-thread-ipc-service-guide.md)** "Shared Memory Invocation" section.

---

## Features to be Introduced

### 1. Middleware/Interceptor Chain (Priority: ⭐⭐⭐)

**Value**: High extensibility, easy testing, separation of cross-cutting concerns

**Design Overview**:
```c
typedef int (*ipc_middleware_t)(ipc_service_t* service,
                                 ipc_request_id_t rid,
                                 const void* data, size_t size,
                                 void** out_data, size_t* out_size,
                                 ipc_middleware_t next);

// Usage example
ipc_service_use(&service, logging_middleware);
ipc_service_use(&service, validation_middleware);
ipc_service_use(&service, timeout_middleware);
```

**Application Scenarios**:
- Logging
- Request validation
- Timeout control
- Metrics collection
- Permission checking

**Implementation Points**:
- Middleware chain management
- Call order guarantee
- Error propagation mechanism

---

### 2. Circuit Breaker Pattern (Priority: ⭐⭐⭐)

**Value**: Improve system resilience, prevent cascading failures

**Design Overview**:
```c
typedef struct ipc_circuit_breaker {
    atomic_t      failure_count;
    atomic_t      state;           // CLOSED, OPEN, HALF_OPEN
    k_timeout_t   open_timeout;
    int           failure_threshold;
} ipc_circuit_breaker_t;

// State transitions
// CLOSED (normal) --failures reach threshold--> OPEN (circuit break)
// OPEN --timeout--> HALF_OPEN (probe)
// HALF_OPEN --success--> CLOSED / --failure--> OPEN
```

**Configuration Options**:
- `CONFIG_THREAD_IPC_CIRCUIT_BREAKER`
- `CONFIG_THREAD_IPC_CB_FAILURE_THRESHOLD` - Failure count to trigger circuit break
- `CONFIG_THREAD_IPC_CB_OPEN_TIMEOUT_MS` - Circuit break duration

---

### 3. Metrics Collection (Priority: ⭐⭐⭐)

**Value**: Observability foundation, performance monitoring

**Design Overview**:
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

**Collected Metrics**:
- Total requests / success / failure / timeout
- Average / max / min latency
- Current pending count
- Queue utilization

**Integration Method**:
- Auto-collect via middleware
- Or embed in core code

---

### 4. Retry Strategy (Priority: ⭐⭐)

**Value**: Improve reliability, handle transient failures

**Design Overview**:
```c
typedef struct ipc_retry_policy {
    int           max_retries;
    k_timeout_t   initial_delay;
    float         backoff_factor;  // Exponential backoff factor
    k_timeout_t   max_delay;
    bool          jitter;          // Add jitter to avoid thundering herd
} ipc_retry_policy_t;

// API
int ipc_call_with_retry(ipc_service_t* service,
                        const void* data, size_t size,
                        ipc_retry_policy_t* policy,
                        void** out_data, size_t* out_size);
```

**Retry Strategies**:
- Fixed interval
- Exponential backoff
- Exponential backoff + jitter

---

### 5. Health Check (Priority: ⭐⭐)

**Value**: DevOps-friendly, service status monitoring

**Design Overview**:
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

**Implementation Method**:
- Built-in heartbeat request (PING/PONG)
- Periodic health check thread
- Consecutive error count

---

### 6. Lock-free Pending Table (Priority: ⭐)

**Value**: Extreme performance, eliminate lock contention

**Design Overview**:
```c
// Lock-free hash table
typedef struct ipc_pending_slot {
    ipc_request_id_t request_id;
    atomic_uintptr_t data;
} ipc_pending_slot_t;

// Use atomic CAS operations
ipc_pending_request_t* find_pending_lockless(ipc_service_t* service,
                                              ipc_request_id_t rid);
```

**Applicable Scenarios**:
- High concurrency (thousands of requests per second)
- Systems with high real-time requirements

**Implementation Complexity**: High

---

### 7. Service Registration and Discovery (Priority: ⭐)

**Value**: Dynamic service discovery, loose coupling

**Design Overview**:
```c
// Service registry
int ipc_registry_register(const char* name, ipc_service_t* service);
ipc_service_t* ipc_registry_lookup(const char* name);

// Convenient call
int ipc_call_by_name(const char* service_name,
                     const void* data, size_t size,
                     void** out, size_t* out_size,
                     k_timeout_t timeout);
```

**Applicable Scenarios**:
- Plugin-based architecture
- Runtime service binding
- Configuration-driven service selection

---

### 8. Distributed Tracing (Priority: ⭐)

**Value**: Distributed debugging, request chain visualization

**Design Overview**:
```c
typedef struct ipc_trace_context {
    uint64_t trace_id;
    uint64_t span_id;
    uint64_t parent_span_id;
    uint32_t flags;
} ipc_trace_context_t;

// Pass in request message
typedef struct ipc_request_msg {
    // ...
    ipc_trace_context_t trace_ctx;
} ipc_request_msg_t;
```

**Integration Method**:
- Integrate with OpenTelemetry
- Custom tracing backend

---

### 9. Event Bus Integration (Priority: ⭐)

**Value**: Event-driven architecture, decoupled subscription

**Design Overview**:
```c
// IPC result as event
typedef struct ipc_event {
    uint32_t          event_type;
    ipc_request_id_t  request_id;
    int               result;
    const void*       data;
    size_t            data_size;
    const char*       service_name;
} ipc_event_t;

// Subscribe
int ipc_subscribe(const char* service_name,
                  ipc_event_handler_t handler,
                  void* user_data);
```

**Current Status**: Basic implementation with `CONFIG_THREAD_IPC_SERVICE_EVENT_BRIDGE` already exists

---

### 10. RPC Code Generation (Priority: ⭐)

**Value**: Development efficiency, type safety

**Design Overview**:
```yaml
# Service definition file (YAML)
service: SensorService
methods:
  - name: read_temperature
    input: TemperatureRequest
    output: TemperatureResponse
```

Generated code:
```c
// Auto-generated
int sensor_read_temperature(float* value, k_timeout_t timeout);
```

---

## Feature Priority Matrix

| Feature | Value | Complexity | Priority |
|---------|-------|------------|----------|
| Zero-copy buffer | High | Medium | ✅ Implemented |
| Middleware/Interceptor | High | Medium | ⭐⭐⭐ |
| Circuit Breaker | High | Medium | ⭐⭐⭐ |
| Metrics Collection | High | Low | ⭐⭐⭐ |
| Retry Strategy | Medium | Low | ⭐⭐ |
| Health Check | Medium | Low | ⭐⭐ |
| Lock-free Pending Table | Medium | High | ⭐ |
| Service Registry | Medium | Medium | ⭐ |
| Distributed Tracing | Medium | High | ⭐ |
| Event Bus Integration | Low | Medium | ⭐ |
| RPC Code Generation | Low | High | ⭐ |

---

## Design Principles

1. **Optionality** - All expansion features via Kconfig, disabled by default
2. **Backward Compatibility** - New APIs do not break existing code
3. **Zero Overhead** - Unused features do not increase code size or runtime overhead
4. **Static Allocation** - Maintain no-dynamic-memory design principle
5. **Thread Safety** - All APIs safe for use in multi-threaded environment

---

## Contribution Guide

If you want to implement a feature, please:

1. Discuss design plan in GitHub Issues
2. Reference existing code style and architecture
3. Add corresponding Kconfig options
4. Write unit tests
5. Update documentation

---

*Document Version: 1.0*
*Last Updated: 2026-04-13*
