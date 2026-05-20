> Language: [中文](../../zh-CN/00-入门/01-5分钟快速体验.md) | **English**

# 5-Minute Quick Start

**Goal**: See the project running in 5 minutes (no development board required, runs on PC simulation)

**Audience**: Anyone who wants to quickly understand what this project can do

---

## Prerequisites (2 minutes)

Confirm you have the following two items installed:

### 1. Python 3.8+

```bash
# Verify command
python --version
# or
python3 --version
```

**Not installed?** → https://www.python.org/downloads/

### 2. Git

```bash
# Verify command
git --version
```

**Not installed?** → https://git-scm.com/downloads

---

## Step 1: Clone the Repository (1 minute)

```bash
# Clone to local
git clone https://github.com/your-username/zeplod.git
cd zeplod
```

---

## Step 2: Install West (1 minute)

```bash
# Install Zephyr build tools
pip install west
```

**Verify installation**:
```bash
west --version
```

---

## Step 3: Configure Zephyr Paths (2 minutes)

### Option A: You already have a Zephyr environment

```bash
# Copy template file
copy zephyr_config.env.template zephyr_config.env
```

Then edit `zephyr_config.env` and fill in your paths:

```bash
# Edit this file, fill in your Zephyr path
ZEPHYR_SDK_INSTALL_DIR=C:/zephyr-sdk
ZEPHYR_BASE=D:/zephyrproject/zephyr
```

### Option B: You don't have a Zephyr environment yet

**Quick approach**: Skip hardware-related features for now and experience `native_posix` (PC simulation) only.

The project requires Zephyr SDK for full builds. If you just want a quick experience:

1. **Read the documentation**: See [docs/en/00-getting-started/02-doc-index.md](02-doc-index.md) to understand the project structure
2. **Browse example code**: Look at source code under `src/`
3. **Install Zephyr and try again**: Refer to [docs/en/10-environment-build/11-environment-setup.md](../10-environment-build/11-environment-setup.md)

> **Tip**: Full environment setup takes about 1-2 hours (including download time). Plan accordingly.

---

## Step 4: Build and Run (2 minutes)

### Build (using native_posix, no development board required)

```bash
# Build PC simulation version
west build -b native_posix .
```

**Success indicator**: You should see this output
```
Built zephyr/zephyr.exe
```

### Run

```bash
# Windows
.\build\zephyr\zephyr.exe

# Linux/macOS
./build/zephyr/zephyr
```

---

## What Do You See?

After successful execution, you should see output similar to:

```
*** Booting Zephyr OS zephyr 3.6.0 ***
[INFO] app: Application starting...
[INFO] module_manager: Module manager initialized
[INFO] event_system: Event system ready
[INFO] app: System status: OK
app>
```

### Try Shell Commands

At the `app>` prompt, enter:

```bash
# View application status
app status

# View module information
app modules

# View event statistics
app events

# View help
app help
```

**Example output**:
```
app> app status
Application: zeplod
Version: 1.0.0
Status: Running
Uptime: 00:00:15
```

---

## What's Next?

### Continue Learning

| I want to... | Read this | Estimated time |
|--------------|-----------|----------------|
| **Set up the full environment properly** | [docs/en/10-environment-build/11-environment-setup.md](../10-environment-build/11-environment-setup.md) | 1-2 hours |
| **Understand how to develop with this project** | [docs/en/00-getting-started/04-developer-guide.md](04-developer-guide.md) | 1 hour |
| **Run on real development hardware** | [docs/en/60-debugging/61-flash-debug-quickstart.md](../60-debugging/61-flash-debug-quickstart.md) | 30 minutes |
| **Control hardware (LED)** | <!-- TODO:等待从零到 Blink LED.md 创建 --> | 30 minutes |
| **Understand the overall architecture** | [docs/en/00-getting-started/02-doc-index.md](02-doc-index.md) | 20 minutes |

### Common Issues

**Q: Build fails, saying Zephyr SDK not found?**  
A: You need to install Zephyr SDK first. Refer to [docs/en/10-environment-build/11-environment-setup.md](../10-environment-build/11-environment-setup.md)

**Q: `west` command not found?**  
A: Make sure you've run `pip install west` and that Python's `Scripts` directory is in your PATH

**Q: Want to run on real hardware?**  
A: You need a full Zephyr SDK installation. Refer to [docs/en/10-environment-build/11-environment-setup.md](../10-environment-build/11-environment-setup.md), then read [docs/en/60-debugging/61-flash-debug-quickstart.md](../60-debugging/61-flash-debug-quickstart.md)

---

## Project Overview

```
zeplod/
├── src/
│   ├── core/           # Core Event System
│   ├── services/       # System services (logging, memory, watchdog)
│   ├── modules/        # Business modules
│   └── app/            # Application entry point
├── tests/              # Unit tests
├── docs/               # Complete documentation
└── scripts/            # Utility scripts
```

**Key Features**:
- **Event-Driven**: Modules communicate via events (Publish-Subscribe pattern)
- **Modular**: Each feature is an independent module, easy to extend
- **Thread-Safe**: Complete synchronization mechanisms
- **Logging System**: Tiered logging for easy debugging
- **Shell Commands**: Built-in debugging commands

---

## Reference Resources

- [Complete Documentation Index](02-doc-index.md) - Master index for all manuals
- [Zephyr Official Documentation](https://docs.zephyrproject.org/) - Zephyr RTOS authoritative guide
- [Project README](../../../README.md) - Project overview and features

---

**Note**: This quick start only demonstrates PC simulation. Full features (hardware control, low power, OTA, etc.) require real development hardware and a complete Zephyr environment.
