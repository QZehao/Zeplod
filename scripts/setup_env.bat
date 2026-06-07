@echo off
REM =============================================================================
REM Zephyr 环境设置脚本 (Windows Batch)
REM =============================================================================
REM 用法：scripts\setup_env.bat
REM =============================================================================

echo ============================================
echo Zephyr 环境设置
echo ============================================

REM 解析 framework / app 布局（config 始终在 framework 目录）
set "ZP_FRAMEWORK_ROOT=%~dp0.."
set "ZP_APP_ROOT=%ZP_FRAMEWORK_ROOT%"
set "ZP_MODE=framework"
set "ZP_CONFIG_FILE=%ZP_FRAMEWORK_ROOT%zephyr_config.env"

for %%I in ("%ZP_FRAMEWORK_ROOT%..") do set "ZP_PARENT_ROOT=%%~fI"
if exist "%ZP_PARENT_ROOT%\CMakeLists.txt" (
    if exist "%ZP_FRAMEWORK_ROOT%prj.conf" (
        findstr /R /C:"add_subdirectory.*framework" "%ZP_PARENT_ROOT%\CMakeLists.txt" >nul 2>&1
        if not errorlevel 1 (
            set "ZP_MODE=app"
            set "ZP_APP_ROOT=%ZP_PARENT_ROOT%"
        )
        findstr /R /C:"TOPLEVEL_BOOTSTRAP" "%ZP_PARENT_ROOT%\CMakeLists.txt" >nul 2>&1
        if not errorlevel 1 (
            set "ZP_MODE=app"
            set "ZP_APP_ROOT=%ZP_PARENT_ROOT%"
        )
    )
)

if defined ZEPHYR_FRAMEWORK_ROOT set "ZP_FRAMEWORK_ROOT=%ZEPHYR_FRAMEWORK_ROOT%"
if defined ZEPHYR_APP_ROOT set "ZP_APP_ROOT=%ZEPHYR_APP_ROOT%"
if defined ZEPHYR_PROJECT_MODE set "ZP_MODE=%ZEPHYR_PROJECT_MODE%"
set "ZP_CONFIG_FILE=%ZP_FRAMEWORK_ROOT%zephyr_config.env"

echo Project mode: %ZP_MODE%
echo Framework:    %ZP_FRAMEWORK_ROOT%
if "%ZP_MODE%"=="app" echo App root:     %ZP_APP_ROOT%

REM 检查配置文件是否存在
if not exist "%ZP_CONFIG_FILE%" (
    echo 错误：找不到 zephyr_config.env！
    echo 请复制 framework/zephyr_config.env.template 到 framework/zephyr_config.env 并编辑路径。
    exit /b 1
)

REM 加载配置
echo 正在从 zephyr_config.env 加载配置...
for /f "tokens=1,* delims==" %%a in ('findstr /v "^#" "%ZP_CONFIG_FILE%" ^| findstr /v "^$"') do (
    set "%%a=%%b"
)

REM 验证路径
if not defined ZEPHYR_BASE (
    echo 错误：配置中未设置 ZEPHYR_BASE！
    exit /b 1
)

if not defined ZEPHYR_SDK_INSTALL_DIR (
    echo 错误：配置中未设置 ZEPHYR_SDK_INSTALL_DIR！
    exit /b 1
)

if not exist "%ZEPHYR_BASE%" (
    echo 错误：ZEPHYR_BASE 路径不存在：%ZEPHYR_BASE%
    exit /b 1
)

if not exist "%ZEPHYR_SDK_INSTALL_DIR%" (
    echo 错误：ZEPHYR_SDK_INSTALL_DIR 路径不存在：%ZEPHYR_SDK_INSTALL_DIR%
    exit /b 1
)

REM Session-only: do not use setx (very slow; writes User registry each run).

REM 添加 Zephyr 工具到 PATH
if exist "%ZEPHYR_SDK_INSTALL_DIR%\arm-zephyr-eabi\bin" (
    set "PATH=%ZEPHYR_SDK_INSTALL_DIR%\arm-zephyr-eabi\bin;%PATH%"
)

if exist "%ZEPHYR_SDK_INSTALL_DIR%\tools\bin" (
    set "PATH=%ZEPHYR_SDK_INSTALL_DIR%\tools\bin;%PATH%"
)

REM 运行 Zephyr 环境设置脚本（如果存在）
if exist "%ZEPHYR_BASE%\scripts\env.bat" (
    echo 正在运行 Zephyr 环境脚本...
    call "%ZEPHYR_BASE%\scripts\env.bat"
)

REM QEMU（west build -t run 需在 CMake 配置时找到 qemu-system-*）
if not defined QEMU_BIN_PATH (
    if exist "C:\Program Files\qemu\qemu-system-arm.exe" (
        set "QEMU_BIN_PATH=C:\Program Files\qemu"
    )
)
if defined QEMU_BIN_PATH (
    if exist "%QEMU_BIN_PATH%\qemu-system-arm.exe" (
        set "PATH=%QEMU_BIN_PATH%;%PATH%"
    )
)

echo ============================================
echo 环境配置成功！
echo ============================================
echo ZEPHYR_BASE=%ZEPHYR_BASE%
echo ZEPHYR_SDK_INSTALL_DIR=%ZEPHYR_SDK_INSTALL_DIR%
if defined QEMU_BIN_PATH echo QEMU_BIN_PATH=%QEMU_BIN_PATH%
echo ============================================
echo.
echo 现在可以构建项目：
echo   west build -b %DEFAULT_BOARD% -d build .
echo QEMU 仿真：
echo   scripts\run_qemu.ps1
echo.
