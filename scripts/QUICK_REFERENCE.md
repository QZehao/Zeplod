# 🚀 商业模块管理 - 快速参考卡片

## 📍 工具位置

```
D:\Code\3-Project\zephyr_template\scripts\
├── proprietary_manage.bat   # Windows 批处理版本
└── proprietary_manage.ps1   # PowerShell 版本（推荐）
```

---

## ⚡ 常用命令（复制即用）

### 1️⃣ 查看所有模块状态

```cmd
cd D:\Code\3-Project\zephyr_template
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 status
```

### 2️⃣ 启用单个模块

```cmd
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 enable event_system_pro
```

### 3️⃣ 禁用单个模块

```cmd
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 disable event_system_pro
```

### 4️⃣ 启用所有模块

```cmd
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 enable-all
```

### 5️⃣ 禁用所有模块（恢复默认）

```cmd
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 disable-all
```

### 6️⃣ 检查配置

```cmd
powershell -ExecutionPolicy Bypass -File scripts\proprietary_manage.ps1 check
```

---

## 🎯 典型工作流

### 流程 1：启用商业模块

```powershell
# 1. 查看当前状态
.\scripts\proprietary_manage.ps1 status

# 2. 启用所需模块
.\scripts\proprietary_manage.ps1 enable event_system_pro
.\scripts\proprietary_manage.ps1 enable mesh_communication

# 3. 重新构建
cd build
cmake ..
ninja
```

### 流程 2：临时禁用问题模块

```powershell
# 1. 禁用有问题的模块
.\scripts\proprietary_manage.ps1 disable mesh_communication

# 2. 重新构建
cd build
cmake ..
ninja

# 3. 问题解决后重新启用
.\scripts\proprietary_manage.ps1 enable mesh_communication
```

### 流程 3：恢复干净环境

```powershell
# 1. 禁用所有商业模块
.\scripts\proprietary_manage.ps1 disable-all

# 2. 或者用 Git 完全重置
git checkout Kconfig
```

---

## 📦 可用模块列表

| 模块名 | 说明 | 依赖 |
|--------|------|------|
| `event_system_pro` | 事件系统专业版 | mesh_communication |
| `mesh_communication` | Mesh 通信框架 | 无 |
| `module_manager_pro` | 模块管理器专业版 | 无 |
| `ota_manager` | OTA 升级管理 | 无 |
| `security_crypto` | 安全加密模块 | 无 |
| `cellular_5g_usb` | 5G USB 驱动 | 无（文件不存在） |
| `usb_host_cdc_ecm` | USB Host CDC/ECM | 无 |

---

## 🔧 如果遇到问题

### 问题 1: PowerShell 禁止运行脚本

```powershell
# 以管理员身份运行 PowerShell，然后执行：
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### 问题 2: 构建失败，提示找不到 Kconfig

```powershell
# 检查模块是否已启用
.\scripts\proprietary_manage.ps1 check

# 如果模块文件不存在，请禁用它
.\scripts\proprietary_manage.ps1 disable cellular_5g_usb
```

### 问题 3: 想要完全重置环境

```bash
# 使用 Git 恢复 Kconfig
git checkout Kconfig

# 然后重新构建
cd build && cmake .. && ninja
```

---

## 💡 提示

- ✅ 修改配置后**必须重新运行 CMake**
- ✅ 默认状态下所有商业模块都是**禁用**的
- ✅ 模块文件应放置在 `src/proprietary/<模块名>/` 目录下
- ✅ 可以使用 `git status` 查看 Kconfig 的修改状态

---

## 📞 需要帮助？

- 📧 联系: china_qzh@163.com
- 📖 详细文档: `src/proprietary/docs/LOCAL_MANAGEMENT_GUIDE.md`

---

**最后更新**: 2026-04-09
