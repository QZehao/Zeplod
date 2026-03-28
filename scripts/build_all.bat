@echo off
REM Windows 批量构建脚本
REM 用于为多个开发板构建项目

setlocal enabledelayedexpansion

echo ============================================
echo Zephyr 批量构建工具 (Windows)
echo ============================================

REM 默认开发板列表
set BOARDS=native_posix nucleo_f429zi nucleo_f767zi disco_l475_iot1

REM 解析参数
set CLEAN_BUILD=
set OUTPUT_DIR=
set TARGET_BOARD=

:parse_args
if "%~1"=="" goto :end_parse_args
if /i "%~1"=="-c" set CLEAN_BUILD=--clean & shift & goto :parse_args
if /i "%~1"=="--clean" set CLEAN_BUILD=--clean & shift & goto :parse_args
if /i "%~1"=="-o" set OUTPUT_DIR=%~2 & shift & shift & goto :parse_args
if /i "%~1"=="--output" set OUTPUT_DIR=%~2 & shift & shift & goto :parse_args
if /i "%~1"=="-b" set TARGET_BOARD=%~2 & shift & shift & goto :parse_args
if /i "%~1"=="--board" set TARGET_BOARD=%~2 & shift & shift & goto :parse_args
if /i "%~1"=="-h" goto :show_help
if /i "%~1"=="--help" goto :show_help
shift
goto :parse_args

:end_parse_args

REM 如果指定了开发板，只构建指定的
if not "%TARGET_BOARD%"=="" set BOARDS=%TARGET_BOARD%

REM 统计
set SUCCESS_COUNT=0
set FAIL_COUNT=0

REM 构建每个开发板
for %%b in (%BOARDS%) do (
    echo.
    echo ============================================
    echo 构建：%%b
    echo ============================================
    
    set BUILD_DIR=build_%%b
    if not "%OUTPUT_DIR%"=="" set BUILD_DIR=%OUTPUT_DIR%\!BUILD_DIR!
    
    set BUILD_CMD=west build -b %%b --build-dir !BUILD_DIR! %CLEAN_BUILD%
    
    echo 命令：!BUILD_CMD!
    echo.
    
    west build -b %%b --build-dir !BUILD_DIR! %CLEAN_BUILD%
    
    if !ERRORLEVEL! EQU 0 (
        echo [OK] 构建成功：%%b
        set /a SUCCESS_COUNT+=1
    ) else (
        echo [FAIL] 构建失败：%%b
        set /a FAIL_COUNT+=1
    )
)

echo.
echo ============================================
echo 构建完成
echo ============================================
echo 成功：%SUCCESS_COUNT%
echo 失败：%FAIL_COUNT%
echo.

if %FAIL_COUNT% GTR 0 exit /b 1
exit /b 0

:show_help
echo 用法：%~nx0 [选项]
echo.
echo 选项:
echo   -c, --clean       清理后构建
echo   -o, --output DIR  输出目录
echo   -b, --board NAME  指定开发板
echo   -h, --help        显示帮助
exit /b 0
