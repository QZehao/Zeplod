# Data Bus Detailed Usage Guide

## Table of Contents

- [Overview](#overview)
- [Core Concepts](#core-concepts)
- [System Architecture](#system-architecture)
- [Memory and Ownership](#memory-and-ownership)
- [Configuration Options](#configuration-options)
- [API Reference](#api-reference)
- [Usage Guide](#usage-guide)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)

---

## Overview

Data Bus is the **named-channel, reference-counted streaming data sharing** module of the Zephyr RTOS framework. It provides a unified publish-consume abstraction for high-frequency data sharing scenarios such as sensor data streams, log streams, and status broadcasts, completely independent of the event system.

### Key Features

| Feature | Description |
|---------|-------------|
| Named Channels | Globally unique names, dynamically created/looked up at runtime |
| Zero-Copy Sharing | Data is copied once into a slab block; multiple consumers share the same memory |
| ISR/Thread Unified Publish | `data_bus_publish()` automatically detects context and adapts |
| Auto-Release | Framework automatically `release`s after consumer callback returns (default) |
| Explicit Retain | For asynchronous hold, call `data_bus_block_retain()` inside callback |
| Event Bridge | Optional bridge to event system for lightweight notifications |
| Memory Determinism | All from pre-allocated slabs; ISR path does not depend on `k_malloc` |

### Data Bus vs Event System

| Dimension | Data Bus | Event System |
|-----------|----------|--------------|
| Data Model | Streaming data blocks (arbitrary size) | Discrete events (fixed size) |
| Consumer Model | Channel subscription (by name) | Event type subscription (by ID) |
| Data Copy | Once (at publish), zero-copy distribution | Possibly multiple (queue copy + dispatch) |
| Lifecycle | Reference-counted, auto/manual release | System-managed, fire-and-forget |
| Use Cases | Sensor streams, large payload sharing | Control commands, status notifications |

---

## Core Concepts

### Channel

A channel is the fundamental communication unit in Data Bus, with a globally unique name:

```c
typedef struct {
    uint32_t        next_seq;           // Next sequence number (wraps at 2^32)
    uint32_t        publish_count;      // Number of publishes
    uint32_t        drop_count;         // Number of drops
    uint32_t        queue_full_count;   // Queue full count
    uint32_t        alloc_fail_count;   // Allocation failure count
    uint32_t        peak_queue_usage;   // Historical peak queue usage
    // ... internal fields
} data_bus_channel_t;
```

**Key Properties**:
- Globally unique name, length limit `CONFIG_DATA_BUS_CHANNEL_NAME_MAX` (default 16)
- Queue depth `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH` (default 16), stores `data_bus_block_t*` pointers
- Max consumers per channel `CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL` (default 4)

### Block

A block is the unit that carries actual data in Data Bus:

```c
struct data_bus_block {
    void*           ptr;        // Data pointer (from slab or k_malloc)
    size_t          len;        // Data length
    atomic_t        ref_count;  // Reference count
    struct k_mem_slab* slab;    // Source slab (NULL = k_malloc)
    uint32_t        seq;        // Monotonically increasing sequence number
};
```

**Memory Layout**:
```
┌────────────────────────────────┐
│ data_bus_block_t (slab alloc)  │  ← Fixed size
├────────────────────────────────┤
│ ptr ──► Data Buffer            │  ← Tiered slab or k_malloc
│         (256B / 1KB / 4KB)     │
└────────────────────────────────┘
```

### Consumer

Consumers are registered on a channel and receive all data from that channel:

```c
typedef struct {
    const char*             name;            // Consumer name
    bool                    manual_release;  // Default false
    data_bus_consume_fn_t   callback;        // Callback function
    void*                   user_data;       // User data
} data_bus_consumer_cfg_t;
```

**Consumption Modes**:
- `manual_release = false` (default): Framework auto `release`s after callback returns
- `manual_release = true`: User manages `release` manually

### Sequence Number

Each block carries a monotonically increasing `seq`, starting from 0, wrapping at 2^32. Can be used for:
- Detecting packet loss (consumer records `last_seq`, compares gap)
- Data ordering (multi-producer scenarios)
- Debug tracing

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                 Application Layer                           │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐         │
│  │  Publisher  │  │ Consumer A   │  │ Consumer B  │         │
│  │ (ISR/Thread)│  │(auto_release)│  │(retain)     │         │
│  └──────┬──────┘  └──────┬───────┘  └──────┬──────┘         │
│         │                │                 │                │
│         │data_bus_publish│                 │                │
│         ▼                │                 │                │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Data Bus Core                          │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │    │
│  │  │ Channel     │  │ Dispatcher  │  │ Slab Pools  │  │    │
│  │  │ (ring_buf)  │  │ (Thread)    │  │ (256/1K/4K) │  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │    │
│  └─────────────────────────────────────────────────────┘    │
│         │                │                │                 │
│         │ k_sem_give     │ dispatch       │                 │
│         ▼                ▼                ▼                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐          │
│  │  Event      │  │  callback A │  │  callback B │          │
│  │  Bridge     │  │  (auto)     │  │  (retain)   │          │
│  └─────────────┘  └─────────────┘  └─────────────┘          │
└─────────────────────────────────────────────────────────────┘
```

### Component Description

1. **Channel Queue**
   - Based on Zephyr `ring_buf`
   - Stores `data_bus_block_t*` pointers (not the data itself)
   - When queue is full, new data is dropped, returning `-ENOBUFS`

2. **Dispatcher Thread**
   - Runs as an independent thread with configurable priority
   - Waits for publish signal via `k_sem_take`
   - After retrieving the block, calls `data_bus_consumer_dispatch` to distribute to all consumers

3. **Slab Memory Pools**
   - `data_bus_block_slab`: Block struct (fixed size)
   - `data_bus_slab_256/1k/4k`: Data buffers (tiered sizes)
   - Thread context can fall back to k_malloc

---

## Memory and Ownership

### Two-Tier Slab Architecture

```
┌─────────────────────────────────────────────────────────────┐
│         Block Struct Slab (data_bus_block_slab)             │
│         CONFIG_DATA_BUS_MAX_BLOCKS (default 32)             │
├─────────────────────────────────────────────────────────────┤
│         Data Buffer Slabs (conditional, enabled by default) │
├─────────────┬─────────────┬─────────────────────────────────┤
│   256B Pool │    1KB Pool │            4KB Pool             │
│  (default 8)│  (default 4)│          (default 2)            │
└─────────────┴─────────────┴─────────────────────────────────┘
```

### Reference-Counted Lifecycle

```
mem_alloc()          → ref_count = 0 (not yet in lifecycle)
  │
  ▼ publish() success
ref_count = 1 (bus holds)
  │
  ▼ dispatch: atomic_add(ref_count, N)  // N = active consumer count
ref_count = 1+N
  │
  ├──► Consumer Callback ──► retain() ──► ref_count++
  │                      │
  │                      └──► Worker Thread ──► release() ──► ref_count--
  │
  ├──► Framework auto_release ──► release() ──► ref_count--
  │
  ▼ dispatcher release()
ref_count = 0 ──► Free data buffer + block struct
```

### Who Allocates, Who Frees

| Phase | Operation | Responsible | Note |
|-------|-----------|-------------|------|
| Publish | `data_bus_publish()` | Data Bus | Allocates block + data buffer, copies data |
| Enqueue | `ring_buf_put()` | Data Bus | Stores block pointer |
| Dispatch | `dispatch()` | Data Bus | Splits reference count, calls callbacks |
| Consume | `retain()` in callback | User | Increments reference, defers release |
| Release | `data_bus_block_release()` | User / Framework | Auto-recycles memory when zero |

**Core Rules**:
- Default `auto_release`: User does **not** need to and should **not** `release` in callback
- Using `retain()`: User **must** `release` after async processing completes

---

## Configuration Options

| Kconfig | Default | Description |
|---------|---------|-------------|
| `CONFIG_DATA_BUS` | n | Enable Data Bus module |
| `CONFIG_DATA_BUS_CHANNEL_NAME_MAX` | 16 | Max channel name length (incl. NUL) |
| `CONFIG_DATA_BUS_MAX_CHANNELS` | 8 | Max number of channels |
| `CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL` | 4 | Max consumers per channel |
| `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH` | 16 | Channel queue depth |
| `CONFIG_DATA_BUS_MAX_BLOCKS` | 32 | Concurrent block limit |
| `CONFIG_DATA_BUS_DISPATCHER_STACK_SIZE` | 2048 | Dispatcher thread stack size |
| `CONFIG_DATA_BUS_DISPATCHER_PRIORITY` | 5 | Dispatcher thread priority |
| `CONFIG_DATA_BUS_SLAB_ENABLE` | y | Enable data slab pools |
| `CONFIG_DATA_BUS_SLAB_256_COUNT` | 8 | 256B slab block count |
| `CONFIG_DATA_BUS_SLAB_1K_COUNT` | 4 | 1KB slab block count |
| `CONFIG_DATA_BUS_SLAB_4K_COUNT` | 2 | 4KB slab block count |
| `CONFIG_DATA_BUS_EVENT_BRIDGE` | y | Bridge to event system |
| `CONFIG_DATA_BUS_DEBUG_REFCNT` | n | Reference count debug assertions |
| `CONFIG_DATA_BUS_LOG_LEVEL` | 3 | Log level (0=OFF, 4=DEBUG) |

### Memory Footprint Estimate

```
Block struct slab:  sizeof(data_bus_block_t) × MAX_BLOCKS
                    ≈ 32B × 32 = 1024 B

Data slabs (default):
  256B × 8 = 2048 B
  1KB  × 4 = 4096 B
  4KB  × 2 = 8192 B
  Total = 14336 B

Channel slab:       sizeof(data_bus_channel_t) × MAX_CHANNELS
                    ≈ 200B × 8 = 1600 B

Dispatcher stack:   2048 B

Total ≈ 1024 + 14336 + 1600 + 2048 = 19008 B (~18.6 KB)
```

---

## API Reference

### Lifecycle

```c
int data_bus_init(void);
int data_bus_deinit(void);
```

- `init()` is idempotent; repeated calls return 0
- `deinit()` blocks until dispatcher thread exits
- **Warning**: Before `deinit()`, ensure all `retain()`ed blocks have been `release()`d

### Channel Management

```c
int  data_bus_channel_create(const char *name, data_bus_channel_t **out_channel);
int  data_bus_channel_destroy(data_bus_channel_t *ch);
data_bus_channel_t *data_bus_channel_find(const char *name);
```

| Return Value | Meaning |
|--------------|---------|
| 0 | Success |
| -EEXIST | Name already exists |
| -EINVAL | Invalid name |
| -ENOMEM | Channel pool exhausted |
| -EBUSY | Still has active consumers |
| -EAGAIN | Queue not empty or dispatch in progress (retry later) |

### Publish

```c
int data_bus_publish(data_bus_channel_t *ch, const void *data, size_t len);
int data_bus_publish_block(data_bus_channel_t *ch, data_bus_block_t *block);
```

- `publish()`: Data is copied into internal block; callable from ISR/thread
- `publish_block()`: Zero-copy; block must be from `data_bus_mem_alloc()`
- Both return `-ENOBUFS` when queue is full

### Consumer Management

```c
int data_bus_consumer_register(data_bus_channel_t *ch,
                                const data_bus_consumer_cfg_t *cfg,
                                data_bus_consumer_t **out_consumer);
int data_bus_consumer_unregister(data_bus_consumer_t *consumer);
```

### Memory Management

```c
void data_bus_block_acquire(data_bus_block_t *block);
void data_bus_block_release(data_bus_block_t *block);
data_bus_block_t *data_bus_block_retain(data_bus_block_t *block);
```

- `acquire()` / `release()`: Reference count operations
- `retain()`: Called inside callback, returns same pointer (ref_count++)

### Statistics

```c
void data_bus_channel_get_stats(const data_bus_channel_t *ch, data_bus_stats_t *stats);
void data_bus_reset_stats(data_bus_channel_t *ch);
```

---

## Usage Guide

### Scenario 1: Simple Read (Recommended, 90% of Cases)

```c
static void imu_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    const imu_data_t *imu = (const imu_data_t *)block->ptr;
    process(imu);
    /* Framework auto-releases; no manual release needed */
}

void imu_init(void)
{
    data_bus_channel_t *ch;
    data_bus_channel_create("imu", &ch);

    data_bus_consumer_cfg_t cfg = {
        .name     = "imu_fusion",
        .callback = imu_consumer_cb,
    };
    data_bus_consumer_register(ch, &cfg, NULL);
}

/* Publish from ISR or thread */
data_bus_publish(imu_ch, &imu_data, sizeof(imu_data));
```

### Scenario 2: Asynchronous Hold (10% of Cases)

```c
static void log_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    /* Steal the block, hand off to work queue for async processing */
    data_bus_block_t *stolen = data_bus_block_retain(block);
    k_work_submit_to_queue(&work_q, &work_item);
    work_item.block = stolen;
}

/* Worker thread */
void work_handler(struct k_work *work)
{
    process(work_item.block->ptr);
    data_bus_block_release(work_item.block);  /* Must release */
}
```

### Scenario 3: Zero-Copy Publish

```c
/* Pre-allocate block */
data_bus_block_t *block = data_bus_mem_alloc(256);
memcpy(block->ptr, sensor_data, 256);
block->len = 256;

/* Publish (ownership transfer) */
data_bus_publish_block(ch, block);
/* Do not access block after successful publish */
```

### Scenario 4: Multi-Consumer Sharing

```c
/* Create channel */
data_bus_channel_create("temperature", &temp_ch);

/* Register display consumer (simple read) */
data_bus_consumer_cfg_t cfg1 = {
    .name     = "display",
    .callback = display_cb,
};
data_bus_consumer_register(temp_ch, &cfg1, NULL);

/* Register logger consumer (retain for async Flash write) */
data_bus_consumer_cfg_t cfg2 = {
    .name     = "logger",
    .callback = logger_cb,
};
data_bus_consumer_register(temp_ch, &cfg2, NULL);

/* One publish, both consumers receive shared references to the same block */
data_bus_publish(temp_ch, &temp, sizeof(temp));
```

---

## Best Practices

### 1. Channel Naming Convention

```c
/* Recommended: module_data_type */
"imu_raw"
"gps_nmea"
"can_vehicle"

/* Not recommended */
"ch1"
"temp"        // Too vague
```

### 2. Consumer Naming Convention

```c
/* Recommended: module_purpose */
"fusion_imu"
"display_temp"
"logger_can"
```

### 3. Message Size Selection

| Data Size | Recommendation |
|-----------|----------------|
| ≤ 48 B | Consider event system inline data (lighter) |
| ≤ 256 B | Data Bus + 256B slab (optimal) |
| ≤ 1 KB | Data Bus + 1KB slab |
| ≤ 4 KB | Data Bus + 4KB slab |
| > 4 KB | Data Bus + k_malloc fallback (non-realtime) |

### 4. Log Level Recommendations

| Phase | Recommended Level |
|-------|-------------------|
| Development/Debug | `CONFIG_DATA_BUS_LOG_LEVEL=4` (DEBUG) |
| Integration Test | `CONFIG_DATA_BUS_LOG_LEVEL=3` (INFO) |
| Production | `CONFIG_DATA_BUS_LOG_LEVEL=1` (ERROR) |

### 5. Avoid Memory Leaks

```c
/* ❌ Wrong: retain without release */
void bad_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    g_block = data_bus_block_retain(block);
    /* Forgot to release */
}

/* ✅ Correct: every retain must have a matching release */
void good_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    g_block = data_bus_block_retain(block);
}
void later(void)
{
    data_bus_block_release(g_block);
    g_block = NULL;
}
```

---

## Troubleshooting

### Publish Returns `-ENOMEM`

**Causes**:
1. Slab exhausted in ISR path (ISR has no k_malloc fallback)
2. Block struct slab exhausted (`CONFIG_DATA_BUS_MAX_BLOCKS` too small)

**Solutions**:
- Increase corresponding slab configuration
- Check for unreleased `retain()`ed blocks
- Consider reducing publish frequency or increasing consumer processing speed

### Publish Returns `-ENOBUFS`

**Cause**: Channel queue is full; consumers are not keeping up

**Solutions**:
- Increase `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH`
- Check if consumer callback is blocking
- Consider using `retain()` + work queue for async consumer processing

### Consumer Callback Not Triggered

**Causes**:
1. Consumer not registered correctly (return value not checked)
2. Consumer was unregistered
3. Channel name misspelled

**Diagnostics**:
```c
/* Check registration return value */
int ret = data_bus_consumer_register(ch, &cfg, &consumer);
if (ret != 0) {
    LOG_ERR("register failed: %d", ret);
}

/* Check channel statistics */
data_bus_stats_t stats;
data_bus_channel_get_stats(ch, &stats);
LOG_INF("consumers=%u publish=%u", stats.consumer_count, stats.publish_count);
```

### `data_bus_deinit()` Hangs

**Cause**: A `retain()`ed block was not released; reference count cannot reach zero

**Solution**: Ensure all async consumers have completed and `release()`d their held blocks

### Reference Count Underflow

**Symptom**: Assertion triggered when `CONFIG_DATA_BUS_DEBUG_REFCNT=y`

**Causes**:
1. `release()` called multiple times on the same block
2. Consumer called `release()` on an auto_release block inside callback

**Solutions**:
- In default mode (`manual_release=false`), do **not** call `release()` inside callback
- Only extra references obtained via `retain()` need to be released by user
