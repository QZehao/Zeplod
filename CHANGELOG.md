# 变更日志 (CHANGELOG)

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/) 规范。

---

## [1.0.0] - 2026-04-10

### 新增
- 初始项目发布
- 完整的事件驱动架构
- 模块化系统设计
- 系统服务层（日志、内存、看门狗、定时器）
- 应用层框架
- 配置管理系统
- 构建和部署脚本

### 特性
- **事件系统**：支持 256 种事件类型，16 个订阅者/事件
- **模块管理**：动态注册、生命周期管理、依赖管理
- **系统服务**：
  - 统一日志服务
  - 内存池管理
  - 看门狗服务
  - 定时器服务
- **示例模块**：
  - 示例模块 A（传感器模拟）
  - 示例模块 B（通信模块）
  - GPIO 控制模块
  - UART 通信模块
  - Thread IPC 模块
  - 多依赖示例模块
- **开发工具**：
  - GitHub Actions CI/CD
  - GitLab CI
  - clang-format 代码格式化
  - clang-tidy 静态分析
  - Doxygen API 文档
  - VSCode 调试配置
  - Pre-commit 钩子

### 改进
- 应用与子系统初始化改为 **`SYS_INIT(POST_KERNEL, APP_INIT_PRIO_*)`**；示例模块在各自 **`.c`** 内注册；**`app_init()`** 仅作初始化完成查询
- 模板落地：根目录 **README**、**docs/开发者入门指南**、**docs/文档索引** 增加「从模板初始化」检查清单
- **docs/CI平台配置保姆级手册.md**：GitHub Actions / GitLab 上启用 CI、查看流水线、改版本与板型、Secrets、Runner 与镜像拉取等逐步说明
- 完善中文文档
- 优化项目结构
- 单元测试：`tests/` 补全与主工程一致的源码链接
- CI：新增 `build-tests`（`native_posix` 构建并 `run` ztest）
- 开发体验：`.pre-commit-config.yaml`、`.clang-tidy`；`APP_VERSION` 单一版本源 + `scripts/bump_version.py`

### 修复
- `west.yml`：默认使用 **`https://github.com/zephyrproject-rtos/zephyr`** 与 **`v3.6.0`**（与 CI 一致），替代不可移植的本地 `file://` 路径
- 根目录版本文件由 `VERSION` 重命名为 `APP_VERSION`，并在 `find_package(Zephyr)` 之前解析，避免与 Zephyr `version.cmake` 对 `VERSION` 文件的解析冲突
- `scripts/build_all.sh` 在 `set -e` 下正确统计失败次数；`scripts/package_release.sh` 去掉无意义的 `cat | tr`

---

---

## 版本说明

### 版本号格式
`主版本号。次版本号.修订号`

- **主版本号**：不兼容的 API 变更
- **次版本号**：向后兼容的功能新增
- **修订号**：向后兼容的问题修正

### 变更类型
- **新增** (Added) - 新功能
- **改进** (Changed) - 已有功能的变更
- **弃用** (Deprecated) - 即将移除的功能
- **移除** (Removed) - 已移除的功能
- **修复** (Fixed) - Bug 修复
- **安全** (Security) - 安全性修复

---

## 发布计划

### v1.1.0 (计划中)
- 完善单元测试覆盖
- 添加更多示例模块
- 改进文档
- 性能优化

### v1.2.0 (计划中)
- 添加低功耗支持
- 增加 OTA 升级模块
- 改进事件系统性能

---

## 贡献者

感谢所有为本项目做出贡献的开发者！

---

## 许可证

本项目采用 GNU General Public License v3.0 (GPL-3.0) 许可证。
