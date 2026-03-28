@echo off
REM =============================================================================
REM Zephyr 环境设置脚本 (Windows Batch)
REM =============================================================================
REM 用法：scripts\setup_env.bat
REM =============================================================================

setlocal enabledelayedexpansion

echo ============================================
echo Zephyr 环境设置
echo ============================================

REM 检查配置文件是否存在
if not exist "%~dp0..\zephyr_config.env" (
    echo 错误：找不到 zephyr_config.env！
    echo 请复制 zephyr_config.env.template 到 zephyr_config.env 并编辑路径。
    exit /b 1
)

REM 加载配置
echo 正在从 zephyr_config.env 加载配置...
for /f "tokens=1,* delims==" %%a in ('findstr /v "^#" "%~dp0..\zephyr_config.env" ^| findstr /v "^$"') do (
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

REM 设置环境变量
echo 正在设置环境变量...
setx ZEPHYR_BASE "%ZEPHYR_BASE%"
setx ZEPHYR_SDK_INSTALL_DIR "%ZEPHYR_SDK_INSTALL_DIR%"

REM 设置当前会话变量
set ZEPHYR_BASE=%ZEPHYR_BASE%
set ZEPHYR_SDK_INSTALL_DIR=%ZEPHYR_SDK_INSTALL_DIR%

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

echo ============================================
echo 环境配置成功！
echo ============================================
echo ZEPHYR_BASE=%ZEPHYR_BASE%
echo ZEPHYR_SDK_INSTALL_DIR=%ZEPHYR_SDK_INSTALL_DIR%
echo ============================================
echo.
echo 现在可以构建项目：
echo   west build -b %DEFAULT_BOARD% .
echo.

endlocal
