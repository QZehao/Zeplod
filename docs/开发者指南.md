# 开发者指南

本指南介绍如何使用 Zephyr 事件驱动项目模板进行开发。

## 目录

1. [快速开始](#快速开始)
2. [项目结构](#项目结构)
3. [开发工作流](#开发工作流)
4. [代码规范](#代码规范)
5. [测试](#测试)
6. [调试](#调试)
7. [发布](#发布)

---

## 快速开始

### 1. 环境配置

```bash
# Windows PowerShell
.\scripts\setup_env.ps1

# Linux/macOS
source scripts/setup_env.sh
```

### 2. 构建项目

```bash
# 构建 native_posix（测试用）
west build -b native_posix .

# 构建目标开发板
west build -b nucleo_f429zi .
```

### 3. 烧录和监控

```bash
# 烧录
west flash

# 串口监控
python scripts/serial_monitor.py -p COM3 -b 115200
```

---

## 项目结构

```
zephyr_template/
├── src/
│   ├── core/           # 核心事件系统
│   ├── services/       # 系统服务
│   ├── modules/        # 业务模块
│   └── app/            # 应用程序
├── tests/              # 单元测试
├── docs/               # 文档
├── scripts/            # 工具脚本
├── .github/workflows/  # CI/CD 配置
└── .vscode/            # VSCode 配置
```

---

## 开发工作流

### 添加新模块

1. 创建模块文件 `src/modules/my_module.h/c`
2. 实现模块接口
3. 在 `CMakeLists.txt` 中添加源文件
4. 在 `app_main.c` 中注册模块

### 添加事件类型

```c
// 在模块头文件中定义
#define EVENT_TYPE_MY_EVENT  100

// 注册事件类型
event_register_type(EVENT_TYPE_MY_EVENT, "my_event");

// 订阅事件
event_subscribe(EVENT_TYPE_MY_EVENT, my_callback, user_data, &id);

// 发布事件
event_publish_copy(EVENT_TYPE_MY_EVENT, EVENT_PRIORITY_NORMAL, &data, sizeof(data));
```

### 修改配置

- `prj.conf` - Zephyr 内核配置
- `Kconfig` - 项目 Kconfig 选项
- `zephyr_config.env` - 本地路径配置
- `src/app/app_config.h` - 应用配置

---

## 代码规范

### 格式化代码

```bash
# 使用 clang-format（仓库根目录 .clang-format）
clang-format -i src/**/*.c src/**/*.h
```

### pre-commit（可选）

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

配置见仓库根目录 `.pre-commit-config.yaml`（含行尾空白、YAML 检查、clang-format）。

### 静态分析（clang-tidy）

仓库提供 `.clang-tidy`（可按项目收紧或放宽检查项）。需先配置 Zephyr 构建以生成 `compile_commands.json`，例如：

```bash
west build -b native_posix . -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/core/event_system.c
```

无 `compile_commands.json` 时，仅能用 `clang-tidy ... -- -I...` 手动传头，易与 Zephyr 不一致，建议优先用 `-p build`。

### 命名约定

| 类型 | 命名风格 | 示例 |
|------|---------|------|
| 文件 | snake_case | `event_system.c` |
| 函数 | snake_case | `event_publish()` |
| 类型 | snake_case_t | `event_t`, `event_status_t` |
| 枚举值 | UPPER_CASE | `EVENT_OK`, `EVENT_ERR` |
| 宏 | UPPER_CASE | `EVENT_MAX_SUBSCRIBERS` |
| 变量 | lower_case | `subscriber_id` |
| 模块名 | snake_case | `module_manager` |

---

## 测试

### 运行单元测试

```bash
# 构建（native_posix）
west build -b native_posix tests/ --build-dir build_tests

# 运行 ztest 可执行文件（Linux/macOS）
./build_tests/zephyr/zephyr

# 或一步完成
west build -t run --build-dir build_tests
```

需已设置 `ZEPHYR_BASE` 或存在仓库根目录的 `zephyr_config.env`。

### 添加新测试

在 `tests/` 目录创建测试文件：

```c
#include <zephyr/ztest.h>
#include <被测试模块.h>

ZTEST(test_module, test_feature)
{
    // 测试代码
    zassert_equal(expected, actual, "错误消息");
}

ZTEST_SUITE(test_module, NULL, NULL, NULL, NULL, NULL);
```

---

## 调试

### VSCode 调试

1. 按 `F5` 启动调试
2. 选择调试配置（QEMU/OpenOCD/J-Link）
3. 使用断点和变量窗口

### 日志调试

```c
// 在代码中添加日志
LOG_INF("信息日志：%d", value);
LOG_DBG("调试日志：%s", string);
LOG_WRN("警告日志");
LOG_ERR("错误日志：%d", error_code);

// 更改日志级别（prj.conf）
CONFIG_LOG_DEFAULT_LEVEL_DBG=y
```

### Shell 命令

```bash
# 应用状态
app status

# 模块信息
app modules

# 事件统计
app events

# 内存统计
app memory

# 日志转储
app log 3
```

---

## 发布

### 版本更新

1. 更新版本号：
   - `CMakeLists.txt`
   - `README.md`
   - `Doxyfile`
   - `CHANGELOG.md`

2. 提交更改：
```bash
git add .
git commit -m "chore: 准备发布 v1.0.0"
```

3. 创建标签：
```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

### 打包固件

```bash
# 打包发布
scripts/package_release.sh -v 1.0.0

# 批量构建
scripts/build_all.sh -c
```

---

## 工具脚本

| 脚本 | 描述 |
|------|------|
| `setup_env.ps1/sh` | 设置环境 |
| `build_all.sh/bat` | 批量构建 |
| `package_release.sh` | 打包发布 |
| `generate_docs.sh` | 生成文档 |
| `serial_monitor.py` | 串口监控 |

---

## 常见问题

### Q: 如何更改目标开发板？

A: 修改构建命令中的 `-b` 参数：
```bash
west build -b nucleo_f767zi . --clean
```

### Q: 如何添加新的 Kconfig 选项？

A: 在 `Kconfig` 文件中添加：
```kconfig
config MY_FEATURE
    bool "Enable my feature"
    default y
    help
      Enable this feature to...
```

然后在 `prj.conf` 中启用：
```
CONFIG_MY_FEATURE=y
```

### Q: 如何调试内存问题？

A: 使用内存统计和泄漏检测：
```c
sys_mem_dump_allocations(SYS_MEM_POOL_GENERAL);
uint32_t leaks = sys_mem_check_leaks(SYS_MEM_POOL_GENERAL);
```

---

## 参考资源

- [Zephyr 官方文档](https://docs.zephyrproject.org/)
- [项目 README](../README.md)
- [配置指南](../docs/SETUP_GUIDE.md)
- [API 文档](../docs/api/html/index.html)
