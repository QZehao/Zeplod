# Data Bus 数据总线详细使用说明

## 目录

- [概述](#概述)
- [核心概念](#核心概念)
- [系统架构](#系统架构)
- [内存与所有权](#内存与所有权)
- [配置选项](#配置选项)
- [API 参考](#api-参考)
- [使用指南](#使用指南)
- [最佳实践](#最佳实践)
- [故障排除](#故障排除)

---

## 概述

Data Bus 是 Zephyr RTOS 框架的**命名通道、引用计数流数据共享**模块。它为传感器数据流、日志流、状态广播等高频数据共享场景提供统一的发布-消费抽象，完全独立于事件系统。

### 主要特性

| 特性 | 描述 |
|------|------|
| 命名通道 | 全局唯一名称，运行时动态创建/查找 |
| 零拷贝共享 | 数据只拷贝一次到 slab 块，多消费者共享同一块内存 |
| ISR / 线程统一发布 | `data_bus_publish()` 自动检测上下文并适配 |
| 自动释放 | 消费者回调返回后框架自动 `release`（默认） |
| 显式 Retain | 需要异步持有时，回调内调用 `data_bus_block_retain()` |
| 事件桥接 | 可选桥接到事件系统，发送轻量级通知 |
| 内存确定性 | 全部来自预分配 slab，ISR 路径不依赖 k_malloc |

### 与事件系统的区别

| 维度 | Data Bus | 事件系统 |
|------|----------|---------|
| 数据模型 | 流式数据块（任意大小） | 离散事件（固定大小） |
| 消费者模式 | 通道订阅（按名称） | 事件类型订阅（按 ID） |
| 数据拷贝 | 一次（发布时），零拷贝分发给消费者 | 可能多次（队列拷贝 + 分发） |
| 生命周期 | 引用计数，自动/手动释放 | 系统管理，发布即忘 |
| 适用场景 | 传感器流、大数据块共享 | 控制命令、状态通知 |

---

## 核心概念

### 通道（Channel）

通道是 Data Bus 中最基本的通信单元，具有全局唯一的名称：

```c
typedef struct {
    uint32_t        next_seq;           // 下一个序列号（2^32 处回绕）
    uint32_t        publish_count;      // 发布次数
    uint32_t        drop_count;         // 丢弃次数
    uint32_t        queue_full_count;   // 队列满次数
    uint32_t        alloc_fail_count;   // 分配失败次数
    uint32_t        peak_queue_usage;   // 历史最大队列使用量
    // ... 内部字段
} data_bus_channel_t;
```

**关键属性**：
- 名称全局唯一，长度限制 `CONFIG_DATA_BUS_CHANNEL_NAME_MAX`（默认 16）
- 队列深度 `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH`（默认 16），存储的是 `data_bus_block_t*` 指针
- 最大消费者数 `CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL`（默认 4）

### 数据块（Block）

数据块是 Data Bus 中承载实际数据的单元：

```c
struct data_bus_block {
    void*           ptr;        // 数据指针（来自 slab 或 k_malloc）
    size_t          len;        // 数据长度
    atomic_t        ref_count;  // 引用计数
    struct k_mem_slab* slab;    // 来源 slab（NULL = k_malloc）
    uint32_t        seq;        // 单调递增序列号
};
```

**内存布局**：
```
┌────────────────────────────────┐
│ data_bus_block_t (slab 分配)    │  ← 固定大小
├────────────────────────────────┤
│ ptr ──► 数据缓冲区               │  ← 分级 slab 或 k_malloc
│         (256B / 1KB / 4KB)      │
└────────────────────────────────┘
```

### 消费者（Consumer）

消费者注册在通道上，接收该通道的所有数据：

```c
typedef struct {
    const char*             name;            // 消费者名称
    bool                    manual_release;  // 默认 false
    data_bus_consume_fn_t   callback;        // 回调函数
    void*                   user_data;       // 用户数据
} data_bus_consumer_cfg_t;
```

**消费模式**：
- `manual_release = false`（默认）：框架在回调返回后自动 `release`
- `manual_release = true`：回调内自行管理 `release`

### 序列号（Sequence Number）

每个数据块附带单调递增的 `seq`，从 0 开始，在 2^32 处回绕。可用于：
- 检测丢包（消费者记录 `last_seq`，对比间隔）
- 数据排序（多生产者场景）
- 调试追踪

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                    │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │  Publisher  │  │ Consumer A  │  │ Consumer B  │         │
│  │  (ISR/Thread)│  │(auto_release│  │(retain)     │         │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘         │
│         │                │                │                 │
│         │ data_bus_publish│                │                 │
│         ▼                │                │                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              Data Bus 核心                           │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │   │
│  │  │ Channel     │  │ Dispatcher  │  │ Slab Pools  │  │   │
│  │  │ (ring_buf)  │  │ (Thread)    │  │ (256/1K/4K) │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────┘   │
│         │                │                │                 │
│         │ k_sem_give     │ dispatch       │                 │
│         ▼                ▼                ▼                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │  Event      │  │  callback A │  │  callback B │         │
│  │  Bridge     │  │  (自动释放)  │  │  (retain)   │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### 组件说明

1. **通道队列（Channel Queue）**
   - 基于 Zephyr `ring_buf` 实现
   - 存储 `data_bus_block_t*` 指针（不是数据本身）
   - 队列满时丢弃新数据，返回 `-ENOBUFS`

2. **分发线程（Dispatcher Thread）**
   - 独立线程运行，优先级可配置
   - 通过 `k_sem_take` 等待发布信号
   - 取出数据块后，调用 `data_bus_consumer_dispatch` 分发给所有消费者

3. **Slab 内存池**
   - `data_bus_block_slab`：块结构体（固定大小）
   - `data_bus_slab_256/1k/4k`：数据缓冲区（分级大小）
   - 线程上下文可回退到 k_malloc

---

## 内存与所有权

### 两级 Slab 架构

```
┌─────────────────────────────────────────────────────────────┐
│              块结构体 Slab (data_bus_block_slab)              │
│              CONFIG_DATA_BUS_MAX_BLOCKS (默认 32)             │
├─────────────────────────────────────────────────────────────┤
│              数据缓冲区 Slab (条件编译，默认启用)               │
├─────────────┬─────────────┬─────────────────────────────────┤
│   256B 池   │    1KB 池   │            4KB 池               │
│  (默认 8块) │  (默认 4块) │          (默认 2块)              │
└─────────────┴─────────────┴─────────────────────────────────┘
```

### 引用计数生命周期

```
mem_alloc()          → ref_count = 0 (未进入生命周期)
  │
  ▼ publish() 成功
ref_count = 1 (bus 持有)
  │
  ▼ dispatch: atomic_add(ref_count, N)  // N = 活跃消费者数
ref_count = 1+N
  │
  ├──► 消费者回调 ──► retain() ──► ref_count++
  │                      │
  │                      └──► 工作线程 ──► release() ──► ref_count--
  │
  ├──► 框架 auto_release ──► release() ──► ref_count--
  │
  ▼ dispatcher release()
ref_count = 0 ──► 释放数据缓冲区 + 块结构体
```

### 谁分配、谁释放

| 阶段 | 操作 | 负责方 | 说明 |
|------|------|--------|------|
| 发布 | `data_bus_publish()` | Data Bus | 分配 block + 数据缓冲区，拷贝数据 |
| 入队 | `ring_buf_put()` | Data Bus | 存储 block 指针 |
| 分发 | `dispatch()` | Data Bus | 拆分引用计数，调用回调 |
| 消费 | 回调内 `retain()` | 用户 | 增加引用，延后释放 |
| 释放 | `data_bus_block_release()` | 用户 / 框架 | 归零时自动回收内存 |

**核心规则**：
- 走默认 `auto_release`：用户 **不需要** 也 **不应该** 在回调内 `release`
- 走 `retain()`：用户 **必须** 在异步处理完后 `release`

---

## 配置选项

| Kconfig | 默认值 | 说明 |
|---------|--------|------|
| `CONFIG_DATA_BUS` | n | 启用 Data Bus 模块 |
| `CONFIG_DATA_BUS_CHANNEL_NAME_MAX` | 16 | 通道名称最大长度（含 NUL） |
| `CONFIG_DATA_BUS_MAX_CHANNELS` | 8 | 最大通道数 |
| `CONFIG_DATA_BUS_MAX_CONSUMERS_PER_CHANNEL` | 4 | 每通道最大消费者数 |
| `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH` | 16 | 通道队列深度 |
| `CONFIG_DATA_BUS_MAX_BLOCKS` | 32 | 并发块上限 |
| `CONFIG_DATA_BUS_DISPATCHER_STACK_SIZE` | 2048 | 分发线程栈大小 |
| `CONFIG_DATA_BUS_DISPATCHER_PRIORITY` | 5 | 分发线程优先级 |
| `CONFIG_DATA_BUS_SLAB_ENABLE` | y | 启用数据 slab 池 |
| `CONFIG_DATA_BUS_SLAB_256_COUNT` | 8 | 256B slab 块数 |
| `CONFIG_DATA_BUS_SLAB_1K_COUNT` | 4 | 1KB slab 块数 |
| `CONFIG_DATA_BUS_SLAB_4K_COUNT` | 2 | 4KB slab 块数 |
| `CONFIG_DATA_BUS_EVENT_BRIDGE` | y | 桥接到事件系统（`DATA_BUS_AVAILABLE` 通知） |
| `CONFIG_DATA_BUS_FALLBACK_WARN_THRESHOLD` | 0 | 通道级 `k_malloc` 回退达此次数后发布 **`DATA_BUS_MEMORY_WARNING`** 事件；0 仅记日志 |
| `CONFIG_DATA_BUS_HEALTH_EVENT_TYPE_ID` | 31 | 内存健康警告事件类型 ID（须与 `DATA_BUS_EVENT_TYPE_ID` 区分） |
| `CONFIG_DATA_BUS_DEBUG_REFCNT` | n | 引用计数调试断言 |
| `CONFIG_DATA_BUS_LOG_LEVEL` | 3 | 日志级别 (0=OFF, 4=DEBUG) |

通道统计 **`data_bus_stats_t`** 另含 **`malloc_fallback_count`**、**`slab_exhausted_count`**；全局聚合见 **`data_bus_get_overview()`**。

### 内存占用估算

```
块结构体 slab:  sizeof(data_bus_block_t) × MAX_BLOCKS
                ≈ 32B × 32 = 1024 B

数据 slab (默认):
  256B × 8 = 2048 B
  1KB  × 4 = 4096 B
  4KB  × 2 = 8192 B
  合计 = 14336 B

通道 slab:      sizeof(data_bus_channel_t) × MAX_CHANNELS
                ≈ 200B × 8 = 1600 B

分发线程栈:     2048 B

总计 ≈ 1024 + 14336 + 1600 + 2048 = 19008 B (~18.6 KB)
```

---

## API 参考

### 生命周期

```c
int data_bus_init(void);
int data_bus_deinit(void);
```

- `init()` 幂等，重复调用返回 0
- `deinit()` 阻塞等待分发线程退出
- **警告**：`deinit()` 前必须确保所有 `retain()` 的块已 `release`

### 通道管理

```c
int  data_bus_channel_create(const char *name, data_bus_channel_t **out_channel);
int  data_bus_channel_destroy(data_bus_channel_t *ch);
data_bus_channel_t *data_bus_channel_find(const char *name);
```

| 返回值 | 含义 |
|--------|------|
| 0 | 成功 |
| -EEXIST | 名称已存在 |
| -EINVAL | 名称非法 |
| -ENOMEM | 通道池耗尽 |
| -EBUSY | 仍有活跃消费者 |
| -EAGAIN | 队列非空或分发中（稍后重试） |

### 发布

```c
int data_bus_publish(data_bus_channel_t *ch, const void *data, size_t len);
int data_bus_publish_block(data_bus_channel_t *ch, data_bus_block_t *block);
```

- `publish()`：数据被拷贝到内部块，ISR/线程均可调用
- `publish_block()`：零拷贝，块必须来自 `data_bus_mem_alloc()`
- 队列满时均返回 `-ENOBUFS`

### 消费者管理

```c
int data_bus_consumer_register(data_bus_channel_t *ch,
                                const data_bus_consumer_cfg_t *cfg,
                                data_bus_consumer_t **out_consumer);
int data_bus_consumer_unregister(data_bus_consumer_t *consumer);
```

### 内存管理

```c
void data_bus_block_acquire(data_bus_block_t *block);
void data_bus_block_release(data_bus_block_t *block);
data_bus_block_t *data_bus_block_retain(data_bus_block_t *block);
```

- `acquire()` / `release()`：引用计数操作
- `retain()`：在回调内调用，返回同一指针（ref_count++）

### 统计

```c
void data_bus_channel_get_stats(const data_bus_channel_t *ch, data_bus_stats_t *stats);
void data_bus_reset_stats(data_bus_channel_t *ch);
```

---

## 使用指南

### 场景一：简单读取（推荐，90% 场景）

```c
static void imu_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    const imu_data_t *imu = (const imu_data_t *)block->ptr;
    process(imu);
    /* 框架自动释放，无需手动 release */
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

/* ISR 或线程中发布 */
data_bus_publish(imu_ch, &imu_data, sizeof(imu_data));
```

### 场景二：异步持有（10% 场景）

```c
static void log_consumer_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    /* 偷走数据块，丢给工作队列异步处理 */
    data_bus_block_t *stolen = data_bus_block_retain(block);
    k_work_submit_to_queue(&work_q, &work_item);
    work_item.block = stolen;
}

/* 工作线程 */
void work_handler(struct k_work *work)
{
    process(work_item.block->ptr);
    data_bus_block_release(work_item.block);  /* 必须释放 */
}
```

### 场景三：零拷贝发布

```c
/* 预分配块 */
data_bus_block_t *block = data_bus_mem_alloc(256);
memcpy(block->ptr, sensor_data, 256);
block->len = 256;

/* 发布（所有权转移） */
data_bus_publish_block(ch, block);
/* 成功后不要再访问 block */
```

### 场景四：多消费者共享

```c
/* 通道创建 */
data_bus_channel_create("temperature", &temp_ch);

/* 注册显示消费者（简单读取） */
data_bus_consumer_cfg_t cfg1 = {
    .name     = "display",
    .callback = display_cb,
};
data_bus_consumer_register(temp_ch, &cfg1, NULL);

/* 注册记录消费者（retain 异步写入 Flash） */
data_bus_consumer_cfg_t cfg2 = {
    .name     = "logger",
    .callback = logger_cb,
};
data_bus_consumer_register(temp_ch, &cfg2, NULL);

/* 一次发布，两个消费者各收到同一块数据的共享引用 */
data_bus_publish(temp_ch, &temp, sizeof(temp));
```

---

## 最佳实践

### 1. 通道命名规范

```c
/* 推荐：模块_数据类型 */
"imu_raw"
"gps_nmea"
"can_vehicle"

/* 不推荐 */
"ch1"
"temp"        // 太笼统
```

### 2. 消费者命名规范

```c
/* 推荐：模块_用途 */
"fusion_imu"
"display_temp"
"logger_can"
```

### 3. 消息大小选择

| 数据大小 | 建议 |
|----------|------|
| ≤ 48 B | 考虑事件系统内联数据（更轻量） |
| ≤ 256 B | Data Bus + 256B slab（最佳） |
| ≤ 1 KB | Data Bus + 1KB slab |
| ≤ 4 KB | Data Bus + 4KB slab |
| > 4 KB | Data Bus + k_malloc 兜底（非实时） |

### 4. 日志级别建议

| 阶段 | 推荐级别 |
|------|----------|
| 开发调试 | `CONFIG_DATA_BUS_LOG_LEVEL=4` (DEBUG) |
| 集成测试 | `CONFIG_DATA_BUS_LOG_LEVEL=3` (INFO) |
| 生产环境 | `CONFIG_DATA_BUS_LOG_LEVEL=1` (ERROR) |

### 5. 避免内存泄漏

```c
/* ❌ 错误：retain 后未释放 */
void bad_cb(data_bus_channel_t *ch, data_bus_block_t *block, void *ud)
{
    g_block = data_bus_block_retain(block);
    /* 忘记 release */
}

/* ✅ 正确：retain 必须有配对的 release */
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

## 故障排除

### 发布返回 `-ENOMEM`

**原因**：
1. ISR 路径中 slab 耗尽（ISR 无 k_malloc 兜底）
2. 块结构体 slab 耗尽（`CONFIG_DATA_BUS_MAX_BLOCKS` 太小）

**解决**：
- 增大对应 slab 配置
- 检查是否有未释放的 `retain()` 块
- 考虑降低发布频率或消费者处理速度

### 发布返回 `-ENOBUFS`

**原因**：通道队列已满，消费者处理不及时

**解决**：
- 增大 `CONFIG_DATA_BUS_CHANNEL_QUEUE_DEPTH`
- 检查消费者回调是否阻塞
- 考虑消费者使用 `retain()` + 工作队列异步处理

### 消费者回调未触发

**原因**：
1. 消费者未正确注册（返回值未检查）
2. 消费者已被注销
3. 通道名称拼写错误

**排查**：
```c
/* 检查注册返回值 */
int ret = data_bus_consumer_register(ch, &cfg, &consumer);
if (ret != 0) {
    LOG_ERR("register failed: %d", ret);
}

/* 检查通道统计 */
data_bus_stats_t stats;
data_bus_channel_get_stats(ch, &stats);
LOG_INF("consumers=%u publish=%u", stats.consumer_count, stats.publish_count);
```

### `data_bus_deinit()` 挂死

**原因**：有 `retain()` 的块未释放，引用计数无法归零

**解决**：确保所有异步消费者已完成并 `release` 了持有的块

### 引用计数下溢

**现象**：`CONFIG_DATA_BUS_DEBUG_REFCNT=y` 时触发断言

**原因**：
1. 对同一块调用了多次 `release()`
2. 消费者回调内对 auto_release 块也调用了 `release()`

**解决**：
- 默认模式下（`manual_release=false`），**不要**在回调内调用 `release()`
- 只有 `retain()` 获取的额外引用需要用户释放
