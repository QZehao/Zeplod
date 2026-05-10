> Language: [中文](../../zh-CN/80-贡献与维护/81-参与贡献与代码规范.md) | **English**

# Contributing and Code Standards

This document explains how to **submit changes** (Issue / Pull Request) to this repository, as well as expectations for **code style, linting tools, and CI**, so that contributors and reviewers are aligned.

**Related**: Root **[README.md](../../../README.md)** "Contributing" section · **[Unit Testing and CI Guide.md](../50-测试与CI/51-单元测试与持续集成说明.md)** · **[Zephyr Version and CI Guide.md](../70-发布与产品化/72-Zephyr版本与CI说明.md)**

---

## 1. Collaboration Workflow (Recommended)

1. **Fork** this repository to your namespace.  
2. **Create a feature branch** from **`main`** (or the project's default branch); naming suggestions: `feature/description`, `fix/description`.  
3. Complete development and **self-testing** on the branch (see "Pre-commit Checklist" below).  
4. Submit a **Pull Request (PR)**: use a **complete sentence** in the title to explain "what changed and why"; the body may include: related Issue, testing approach, breaking change description.  
5. Revise based on reviewer feedback; **keep commit history clean** (rebase / squash as per maintainer requirements).

For large changes, open an **Issue** first to discuss direction and avoid conflicts with maintainer expectations.

---

## 2. Pre-commit Checklist

| Item | Description |
|------|-------------|
| **Build** | At minimum **`west build -b <your-target-board> .`** passes; if changing common code, also build **`native_posix`** + **`tests/`** (see **`tests/README.md`**). |
| **Format** | C code conforms to repository **`.clang-format`** (see next section). |
| **Test** | Related **ztest** cases pass; new logic should ideally include test coverage. |
| **Documentation** | If changing `Kconfig`, public APIs, or behavior, update **`docs/`** accordingly (at minimum **[Project Configuration Guide.md](../40-应用开发/42-项目配置项说明.md)** or topic手册) and necessary comments. |

---

## 3. Code Style and Static Analysis

- **Formatting**: Use **clang-format** with the style file at repository root **`.clang-format`**.  
  Example: `clang-format -i src/path/to/file.c`  
- **Static Analysis (optional)**: **`.clang-tidy`** can be used with **`compile_commands.json`** via IDE or command line (see **[Developer Getting Started Guide.md](../00-入门/04-开发者入门指南.md)**).  
  - **Pre-commit Hook (optional)**: If using **pre-commit**, configuration is in **`.pre-commit-config.yaml`** (install: `pip install pre-commit && pre-commit install`).

---

## 3.5 Commit Message Format Specification

This repository uses **Conventional Commits** style commit format, ensuring clear, traceable commit history and facilitating automatic changelog generation.

### 3.5.1 Basic Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Single-line commit example**:
```bash
git commit -m "feat(sys_memory): add heap leak detection"
```

**Multi-line commit example**:
```bash
git commit -m "feat(event_system): optimize event queue performance

- Add queue overflow statistics
- Improve event drop logging
- Add high water mark monitoring

Closes: #123"
```

### 3.5.2 `<type>` Specification (Required)

| Type | Description | When to Use |
|------|-------------|-------------|
| **`feat`** | New feature | Add new feature, interface, or module |
| **`fix`** | Bug fix | Fix incorrect behavior or logic漏洞 |
| **`docs`** | Documentation change | Modify README, manuals, comments (no logic change) |
| **`style`** | Code formatting | Formatting, whitespace, indentation (no logic change) |
| **`refactor`** | Refactoring | Code structure changes without changing functionality or API |
| **`perf`** | Performance optimization | Improve performance, reduce memory/CPU usage |
| **`test`** | Test related | Add/modify test cases, test configuration |
| **`build`** | Build system | Modify CMakeLists.txt, Kconfig, CI configuration |
| **`ci`** | CI/CD | Modify GitHub Actions, GitLab CI scripts |
| **`chore`** | Other chores | Version bumps, dependency updates, tool scripts, etc. |

**Discouraged types**: ~~`update`~~, ~~`modify`~~, ~~`change`~~, ~~`apply`~~ (too vague)

### 3.5.3 `<scope>` Scope (Optional but Recommended)

Indicates the affected area of the change for quick localization:

| Scope | Description | Example |
|-------|-------------|---------|
| **`event`** | Event system core | `event_system`, `event_queue`, `event_dispatcher` |
| **`module`** | Module manager | `module_manager`, business modules |
| **`service`** | System services | `sys_log`, `sys_memory`, `sys_timer`, `sys_watchdog` |
| **`ipc`** | Thread IPC service | `ipc_service` |
| **`app`** | Application main logic | `app_main`, `app_config`, `app_version` |
| **`board`** | Board support | `boards/`, device tree overlay |
| **`ci`** | Continuous integration | GitHub Actions, GitLab CI |
| **`docs`** | Documentation | Manuals under `docs/` |
| **`kconfig`** | Kconfig configuration | `Kconfig`, `prj.conf`, `prj_*.conf` |

**Examples**:
```
feat(event): add EVENT_TYPE_SENSOR_DATA type
fix(module): fix module startup order issue
docs(ci): update CI board configuration documentation
```

### 3.5.4 `<subject>` Title (Required)

**Rules**:
- ✅ Use **imperative mood** present tense ("add" not "added"/"adds")
- ✅ Start with **lowercase** (unless it's a proper noun)
- ✅ **No period** at the end
- ✅ Length ≤50 characters (Git standard)
- ✅ **Chinese commits**: Use concise Chinese phrases (e.g., "增加堆泄漏检测" not "增加了堆泄漏检测功能")

**Good examples**:
```
feat(sys_memory): add heap leak detection
fix(event): fix event queue statistics error
docs(app): update app_kv usage example
```

**Bad examples**:
```
修改                          # Too vague
修复 bug                      # No scope
添加功能                      # No indication of what feature
更新了代码                    # No information
feat: add new feature         # No scope, unclear
FIX: 修复严重问题              # Uppercase, exclamation mark
```

### 3.5.5 `<body>` Body (Optional)

**Multi-line motivation explanation** (suitable for complex changes):
- Explain **why** the change is needed (not "what was done")
- Each line ≤72 characters
- Briefly describe background, alternatives considered, potential impact

**Example**:
```
Previous event statistics only counted total events.
Adding per-type counters helps identify hot paths and
debug event storms.

Trade-off: increases event_system_cb_t by 4 bytes.
```

### 3.5.6 `<footer>` Footer (Optional)

Reference Issues or indicate breaking changes:
```
Closes: #123
Closes: #456
References: #789

BREAKING CHANGE: event_subscribe() now requires non-NULL callback.
```

### 3.5.7 Common Commit Examples

| Scenario | Example Commit Message |
|----------|------------------------|
| Add new feature | `feat(module): add example_module_gpio module` |
| Bug fix | `fix(event): fix event dispatcher ISR race condition` |
| Performance optimization | `perf(sys_memory): optimize first-fit algorithm` |
| Documentation update | `docs(docs): update Thread_IPC integration guide` |
| Refactor code | `refactor(event): simplify event_system_init flow` |
| Formatting | `style(src): clang-format event_system.c` |
| Test case | `test(event): add event queue overflow test` |
| CI configuration | `ci(github): add nucleo_l4r5zi build task` |

### 3.5.8 Git Configuration Suggestions

**Set default editor and auto trim**:
```bash
git config commit.verbose true
git config grep.lineNumber true
```

**Commit Template (optional)**:
Create `.gitmessage` in project root:
```
<type>(<scope>): <subject>

<motivation>
Why this change is needed.

<implementation>
How the problem is solved.

<footer>
Closes: #
```

Load template:
```bash
git config commit.template .gitmessage
```

---

## 4. Continuous Integration (CI)

GitHub Actions is in **`.github/workflows/ci.yml`**; GitLab is in **`.gitlab-ci.yml`**. For steps to enable and troubleshoot CI on these platforms, see **[CI Platform Configuration Guide.md](../50-测试与CI/52-CI平台配置保姆级手册.md)**. Pipelines typically include:

- **ShellCheck**: Static analysis for **`scripts/*.sh`**.  
- **pre-commit**: Aligned with **`.pre-commit-config.yaml`** (includes **clang-format**, YAML, trailing whitespace, etc.); locally install with `pip install pre-commit && pre-commit install` to align with CI.  
- **Build / Test**: **`native_posix`** main project and **`tests/`**, multi-ARM board matrix smoke tests, etc.

Local Zephyr and CI image versions should be aligned; see **[Zephyr Version and CI Guide.md](../70-发布与产品化/72-Zephyr版本与CI说明.md)**. Ensure CI is **green** before merging PRs or explain failure reasons (e.g., known limitations triggered by documentation-only changes).

---

## 5. Change Scope and Review-friendly Practices

- **Each PR focuses on a single topic**, avoid "顺便重构" unrelated files, for easier review and rollback.  
- **Do not commit** local paths, secrets, large binaries (for secrets-related topics, see **[Security and Key Management Guide.md](../70-发布与产品化/75-安全与密钥管理说明.md)**).  
- For changes to **`prj.conf` / `app.overlay`**, if they affect RAM or board-level behavior, please explain **target board** and **verification method** in the PR description.

---

## 6. License

Contributed content is by default subject to the repository root **[LICENSE](../../../LICENSE)** (GPL-3.0). If introducing third-party code, **source and license compatibility** must be explained in the PR.
