# 软件版本管理指南

本项目的软件版本管理系统提供完整的版本跟踪、构建信息和 Git 集成。

## 版本信息组成

### 1. 版本号

采用语义化版本（Semantic Versioning）：`主版本号。次版本号.修订号`

- **主版本号**：不兼容的 API 变更
- **次版本号**：向后兼容的功能新增
- **修订号**：向后兼容的问题修正

**单一来源**：仓库根目录 `APP_VERSION` 文件（单行 `X.Y.Z`）。`CMakeLists.txt` 在配置阶段读取该文件并定义 `PROJECT_VERSION_*`；更新对外版本时请编辑 `APP_VERSION`，或运行：

**请勿**使用根目录文件名 `VERSION`：Zephyr 的 `find_package(Zephyr)` 会按该文件名解析内核版本字段，导致 `VERSION_MAJOR must be present` 等配置错误。

```bash
python scripts/bump_version.py 1.0.1
```

上述脚本会同步 `Doxyfile` 中的 `PROJECT_NUMBER` 与 `README.md` 中的「**版本**」行；提交前请 `git diff` 核对。

### 2. Git 信息

- **Commit Hash**：当前提交的短哈希
- **Branch**：构建时的分支名
- **Tag**：Git 标签（如果有）
- **Dirty Flag**：是否有未提交的更改

### 3. 构建信息

- **编译时间**：`YYYY-MM-DD HH:MM:SS`
- **编译日期**：`YYYY-MM-DD`
- **编译时间**：`HH:MM:SS`
- **目标开发板**：构建目标

## 日志输出示例

启动时日志输出：

```
[00:00:00.100] ========================================
[00:00:00.101]   Application Version Information
[00:00:00.102] ========================================
[00:00:00.103]   Version:     1.0.0
[00:00:00.104]   Version Code: 0x010000
[00:00:00.105]   Git Commit:  a1b2c3d
[00:00:00.106]   Git Branch:  main
[00:00:00.107]   Git Tag:     v1.0.0
[00:00:00.108]   Build Time:  2026-03-28 14:30:00
[00:00:00.109]   Build Target: nucleo_f429zi
[00:00:00.110]   Build Type:  Release
[00:00:00.111]   Compiler:    GCC 12.2.1
[00:00:00.112] ========================================
```

## 使用方法

### 获取版本信息

```c
#include "app_version.h"

// 获取版本字符串
char version[VERSION_STRING_MAX_LEN];
app_version_get_string(version, sizeof(version));
// 输出："1.0.0"

// 获取完整版本信息
char info[VERSION_INFO_STRING_MAX_LEN];
app_version_get_info_string(info, sizeof(info));
// 输出："v1.0.0 (a1b2c3d) [Release] 2026-03-28 14:30:00 - nucleo_f429zi"

// 获取版本号
uint32_t code = app_version_get_code();  // 0x010000
uint8_t major = app_version_get_major();  // 1
uint8_t minor = app_version_get_minor();  // 0
uint8_t patch = app_version_get_patch();  // 0

// 获取 Git 信息
const char *commit = app_version_get_git_commit();  // "a1b2c3d"
const char *branch = app_version_get_git_branch();  // "main"

// 获取构建时间
const char *timestamp = app_version_get_build_timestamp();  // "2026-03-28 14:30:00"

// 打印版本信息到日志
app_version_print();

// 版本检查
if (app_version_check(1, 0, 0)) {
    // 版本匹配
}
```

### Shell 命令

```bash
# 查看版本信息
version

# 应用状态（包含版本）
app status
```

### 版本比较

```c
#include "app_version.h"

// 检查版本是否至少为 1.0.0
if (VERSION_AT_LEAST(1, 0, 0)) {
    // 功能可用
}

// 检查版本是否正好为 1.0.0
if (VERSION_IS(1, 0, 0)) {
    // 精确版本匹配
}

// 检查版本是否不超过 2.0.0
if (VERSION_AT_MOST(2, 0, 0)) {
    // 在范围内
}
```

## 修改版本号

### 方法 1：编辑 CMakeLists.txt

```cmake
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
```

### 方法 2：使用 Git 标签

```bash
# 创建版本标签
git tag -a v1.1.0 -m "Release version 1.1.0"

# 推送标签
git push origin v1.1.0
```

## 版本编码

版本编码为 24 位整数：

```
位 23-16: 主版本号 (8 位)
位 15-8:  次版本号 (8 位)
位 7-0:   修订号 (8 位)

示例：1.2.3 = 0x010203 = 66051
```

## 自动生成的文件

构建时 CMake 会自动生成以下文件：

- `build/generated/app_version_config.h` - 包含所有版本宏定义

## CI/CD 集成

GitHub Actions 会自动在每次构建时记录版本信息：

```yaml
- name: 获取版本信息
  run: |
    echo "Version: ${{ github.ref_name }}"
    echo "Commit: ${{ github.sha }}"
```

## 最佳实践

1. **每次发布前更新版本号** - 遵循语义化版本规范
2. **使用 Git 标签** - 便于追踪发布版本
3. **记录变更日志** - 更新 CHANGELOG.md
4. **检查版本兼容性** - 在模块通信时验证版本
5. **保留版本信息** - 在固件中嵌入版本字符串

## 故障排除

### 问题：版本信息显示 "unknown"

**原因**：Git 未找到或不在 Git 仓库中

**解决**：
```bash
# 确保在 Git 仓库中
git status

# 初始化 Git（如果需要）
git init
git add .
git commit -m "Initial commit"
```

### 问题：编译时间不更新

**原因**：CMake 缓存

**解决**：
```bash
# 清理并重新构建
rm -rf build/
west build -b <board> .
```

### 问题：Git Dirty 标志始终为 1

**原因**：有未提交的更改或生成的文件

**解决**：
```bash
# 提交更改
git add .
git commit -m "Update"

# 或清理生成的文件
git clean -fdx
```

## 相关文件

| 文件 | 描述 |
|------|------|
| `src/app/app_version.h` | 版本 API 头文件 |
| `src/app/app_version.c` | 版本 API 实现 |
| `src/app/app_version_config.h.in` | CMake 模板 |
| `CMakeLists.txt` | 版本配置 |

## 参考

- [语义化版本 2.0.0](https://semver.org/lang/zh-CN/)
- [Git 版本控制](https://git-scm.com/book/zh/v2)
