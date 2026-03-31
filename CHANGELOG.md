# 变更日志 (CHANGELOG)

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/) 规范。

## [未发布]

### 修复
- `west.yml`：默认使用 **`https://github.com/zephyrproject-rtos/zephyr`** 与 **`v3.6.0`**（与 CI 一致），替代不可移植的本地 `file://` 路径；`scripts/build_all.sh` 在 `set -e` 下正确统计失败次数；`scripts/package_release.sh` 去掉无意义的 `cat | tr`。
- 根目录版本文件由 `VERSION` 重命名为 `APP_VERSION`，并在 `find_package(Zephyr)` 之前解析，避免与 Zephyr `version.cmake` 对 `VERSION` 文件的解析冲突（`VERSION_MAJOR must be present`）。

### 新增
- 初始项目模板
- 事件驱动核心系统
- 模块管理器
- 系统服务（日志、内存、看门狗、定时器）
- 示例模块 A（传感器模拟）
- 示例模块 B（通信模块）
- CI/CD 配置（GitHub Actions）
- 代码规范配置（clang-format, clang-tidy）
- API 文档生成（Doxygen）
- VSCode 调试配置
- 环境设置脚本

### 改进
- **docs/CI平台配置保姆级手册.md**：GitHub Actions / GitLab 上启用 CI、查看流水线、改版本与板型、Secrets、Runner 与镜像拉取等逐步说明；根目录 **`.gitlab-ci.yml`** 与 **`.github/workflows/ci.yml`** 能力对齐。**README**、**文档索引**、**Zephyr版本与CI说明**、**单元测试与持续集成说明**、**开发者入门指南**、**参与贡献与代码规范** 增加交叉引用。
- 模板落地：根目录 **README**、**docs/开发者入门指南**、**docs/文档索引** 增加「从模板初始化」检查清单；**`.gitignore`** 忽略 **`zephyr_config.env`** 并补充常见密钥文件名；**`.github/pull_request_template.md`**、**ISSUE_TEMPLATE**；**CI** 增加 **ShellCheck** 与 **`pre-commit run --all-files`**（与 **`.pre-commit-config.yaml`** 对齐）；**docs/参与贡献与代码规范**、**docs/单元测试与持续集成说明** 同步 CI 说明。
- 完善中文文档
- 优化项目结构
- 单元测试：`tests/` 补全与主工程一致的源码链接，新增 `test_module_manager`、`test_sys_memory`、`test_sys_timer`；`tests/Kconfig` 复用根目录 Kconfig
- 文档：根目录 README 更新项目结构、文档索引、单元测试与 Zephyr 版本说明；修正自定义 `BOARD_ROOT` 的启用方式说明
- CI：新增 `build-tests`（`native_posix` 构建并 `run` ztest）
- 模板扩展文档：`docs/OTA与存储扩展指南.md`（OTA/NVS/低功耗指引）、`docs/Zephyr版本与CI说明.md`（与 CI/SDK 对齐）
- 测试：`test_event_dispatcher`、`test_sys_watchdog`、`test_ipc_service`（Thread IPC 烟测）；`tests/prj.conf` 默认开启 IPC 并加大堆
- 开发体验：`.pre-commit-config.yaml`、`.clang-tidy`；`APP_VERSION` 单一版本源 + `scripts/bump_version.py`；`CMakeLists.txt` 从 `APP_VERSION` 读取工程版本（避免根目录同名 `VERSION` 与 Zephyr `find_package` 冲突）

---

## [1.0.0] - 2026-03-28

### 新增
- 初始版本发布
- 完整的事件驱动架构
- 模块化系统设计
- 系统服务层
- 应用层框架
- 配置管理系统
- 构建和部署脚本

### 特性
- **事件系统**：支持 256 种事件类型，16 个订阅者/事件
- **模块管理**：动态注册、生命周期管理
- **系统服务**：
  - 统一日志服务
  - 内存池管理
  - 看门狗服务
  - 定时器服务
- **开发工具**：
  - GitHub Actions CI/CD
  - clang-format 代码格式化
  - clang-tidy 静态分析
  - Doxygen API 文档
  - VSCode 调试配置

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
- 添加更多示例模块
- 增加单元测试
- 改进文档

### v1.2.0 (计划中)
- 添加低功耗支持
- 增加 OTA 升级模块
- 改进事件系统性能

---

## 贡献者

感谢所有为本项目做出贡献的开发者！

---

## 许可证

本项目采用 Apache License 2.0 许可证。
