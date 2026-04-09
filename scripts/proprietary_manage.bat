@echo off
REM ============================================================================
REM 商业模块本地管理脚本 (Windows 批处理)
REM 用法: proprietary_manage.bat [命令] [模块名]
REM 
REM 命令列表:
REM   status          - 查看所有商业模块状态
REM   enable   <模块>  - 启用模块（取消 Kconfig 注释）
REM   disable  <模块>  - 禁用模块（添加 Kconfig 注释）
REM   enable-all      - 启用所有商业模块
REM   disable-all     - 禁用所有商业模块
REM   check           - 检查 Kconfig 配置是否正确
REM   help            - 显示帮助信息
REM
REM 示例:
REM   proprietary_manage.bat status
REM   proprietary_manage.bat enable event_system_pro
REM   proprietary_manage.bat disable-all
REM ============================================================================

setlocal enabledelayedexpansion

REM 配置
set "KCONFIG_FILE=Kconfig"
set "PROPRIETARY_DIR=src\proprietary"

REM 模块列表
set "MODULES=event_system_pro mesh_communication module_manager_pro ota_manager security_crypto cellular_5g_usb usb_host_cdc_ecm"

REM 颜色代码（Windows 10+ 支持）
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "CYAN=[96m"
set "RESET=[0m"

REM ============================================================================
REM 主逻辑
REM ============================================================================

if "%~1"=="" goto :show_help

if /i "%~1"=="status" goto :show_status
if /i "%~1"=="enable" goto :enable_module
if /i "%~1"=="disable" goto :disable_module
if /i "%~1"=="enable-all" goto :enable_all
if /i "%~1"=="disable-all" goto :disable_all
if /i "%~1"=="check" goto :check_config
if /i "%~1"=="help" goto :show_help

echo %RED%未知命令: %~1%RESET%
goto :show_help

REM ============================================================================
REM 显示状态
REM ============================================================================
:show_status
echo.
echo %CYAN%==========================================%RESET%
echo   商业模块状态
echo %CYAN%==========================================%RESET%
echo.

REM 检查 Kconfig 是否存在
if not exist "%KCONFIG_FILE%" (
    echo %RED%错误: 找不到 %KCONFIG_FILE%%RESET%
    exit /b 1
)

REM 检查每个模块
for %%M in (%MODULES%) do (
    set "MODULE_PATH=%PROPRIETARY_DIR%\%%M"
    
    REM 检查目录是否存在
    if exist "!MODULE_PATH!\Kconfig" (
        set "DIR_STATUS=%GREEN%✓ 已就位%RESET%"
    ) else if exist "!MODULE_PATH!" (
        set "DIR_STATUS=%YELLOW%⚠ 目录存在但无 Kconfig%RESET%"
    ) else (
        set "DIR_STATUS=%RED%✗ 不存在%RESET%"
    )
    
    REM 检查 Kconfig 中是否启用
    findstr /C:"source \"%PROPRIETARY_DIR%\%%M\Kconfig\"" "%KCONFIG_FILE%" >nul 2>&1
    if !errorlevel! equ 0 (
        set "KCONFIG_STATUS=%GREEN%✓ 已启用%RESET%"
    ) else (
        findstr /C:"# source \"%PROPRIETARY_DIR%\%%M\Kconfig\"" "%KCONFIG_FILE%" >nul 2>&1
        if !errorlevel! equ 0 (
            set "KCONFIG_STATUS=%YELLOW%- 已禁用%RESET%"
        ) else (
            set "KCONFIG_STATUS=%RED%✗ 未配置%RESET%"
        )
    )
    
    echo %%M
    echo   文件状态: !DIR_STATUS!
    echo   Kconfig:  !KCONFIG_STATUS!
    echo.
)

goto :eof

REM ============================================================================
REM 启用单个模块
REM ============================================================================
:enable_module
if "%~2"=="" (
    echo %RED%错误: 请指定模块名%RESET%
    echo 用法: %0 enable ^<模块名^>
    echo.
    echo 可用模块: %MODULES%
    exit /b 1
)

set "MODULE_NAME=%~2"
set "KCONFIG_LINE=source \"%PROPRIETARY_DIR%\%MODULE_NAME%\Kconfig\""

echo.
echo %CYAN%启用模块: %MODULE_NAME%%RESET%

REM 检查模块是否存在
if not exist "%PROPRIETARY_DIR%\%MODULE_NAME%\Kconfig" (
    echo %RED%错误: 模块 %MODULE_NAME% 的 Kconfig 不存在%RESET%
    echo 请先将模块文件放置到 %PROPRIETARY_DIR%\%MODULE_NAME%\
    exit /b 1
)

REM 取消注释（将 "# source ..." 替换为 "source ..."）
powershell -Command "(Get-Content '%KCONFIG_FILE%') -replace '# (source \"%PROPRIETARY_DIR%\\%MODULE_NAME%\\Kconfig\")', '$1' | Set-Content '%KCONFIG_FILE%'"

if !errorlevel! equ 0 (
    echo %GREEN%✓ 模块 %MODULE_NAME% 已启用%RESET%
    echo.
    echo 提示: 请重新运行 CMake 配置以应用更改
) else (
    echo %RED%✗ 启用失败%RESET%
)

goto :eof

REM ============================================================================
REM 禁用单个模块
REM ============================================================================
:disable_module
if "%~2"=="" (
    echo %RED%错误: 请指定模块名%RESET%
    echo 用法: %0 disable ^<模块名^>
    echo.
    echo 可用模块: %MODULES%
    exit /b 1
)

set "MODULE_NAME=%~2"

echo.
echo %CYAN%禁用模块: %MODULE_NAME%%RESET%

REM 添加注释（将 "source ..." 替换为 "# source ..."）
powershell -Command "(Get-Content '%KCONFIG_FILE%') -replace '(source \"%PROPRIETARY_DIR%\\%MODULE_NAME%\\Kconfig\")', '# $1' | Set-Content '%KCONFIG_FILE%'"

if !errorlevel! equ 0 (
    echo %GREEN%✓ 模块 %MODULE_NAME% 已禁用%RESET%
    echo.
    echo 提示: 请重新运行 CMake 配置以应用更改
) else (
    echo %RED%✗ 禁用失败%RESET%
)

goto :eof

REM ============================================================================
REM 启用所有模块
REM ============================================================================
:enable_all
echo.
echo %CYAN%==========================================%RESET%
echo   启用所有商业模块
echo %CYAN%==========================================%RESET%
echo.

for %%M in (%MODULES%) do (
    if exist "%PROPRIETARY_DIR%\%%M\Kconfig" (
        powershell -Command "(Get-Content '%KCONFIG_FILE%') -replace '# (source \"%PROPRIETARY_DIR%\\%%M\\Kconfig\")', '$1' | Set-Content '%KCONFIG_FILE%'"
        echo %GREEN%✓ 启用: %%M%RESET%
    ) else (
        echo %YELLOW%⚠ 跳过: %%M (Kconfig 不存在)%RESET%
    )
)

echo.
echo %GREEN%✓ 所有可用模块已启用%RESET%
echo 提示: 请重新运行 CMake 配置以应用更改
goto :eof

REM ============================================================================
REM 禁用所有模块
REM ============================================================================
:disable_all
echo.
echo %CYAN%==========================================%RESET%
echo   禁用所有商业模块
echo %CYAN%==========================================%RESET%
echo.

for %%M in (%MODULES%) do (
    powershell -Command "(Get-Content '%KCONFIG_FILE%') -replace '(source \"%PROPRIETARY_DIR%\\%%M\\Kconfig\")', '# $1' | Set-Content '%KCONFIG_FILE%'"
    echo %GREEN%✓ 禁用: %%M%RESET%
)

echo.
echo %GREEN%✓ 所有商业模块已禁用%RESET%
echo 提示: 请重新运行 CMake 配置以应用更改
goto :eof

REM ============================================================================
REM 检查配置
REM ============================================================================
:check_config
echo.
echo %CYAN%==========================================%RESET%
echo   检查商业模块配置
echo %CYAN%==========================================%RESET%
echo.

set "ERRORS=0"

REM 检查 Kconfig 文件
if not exist "%KCONFIG_FILE%" (
    echo %RED%✗ 错误: 找不到 %KCONFIG_FILE%%RESET%
    exit /b 1
)

REM 检查每个模块的配置
for %%M in (%MODULES%) do (
    set "MODULE_PATH=%PROPRIETARY_DIR%\%%M"
    set "MODULE_OK=true"
    
    REM 检查 Kconfig 中是否有该模块的配置
    findstr /C:"%%M\Kconfig" "%KCONFIG_FILE%" >nul 2>&1
    if !errorlevel! neq 0 (
        echo %RED%✗ %%M: Kconfig 中未配置%RESET%
        set "MODULE_OK=false"
        set /a ERRORS+=1
    )
    
    REM 如果配置了，检查是否启用
    if "!MODULE_OK!"=="true" (
        findstr /C:"source \"%PROPRIETARY_DIR%\%%M\Kconfig\"" "%KCONFIG_FILE%" >nul 2>&1
        if !errorlevel! equ 0 (
            REM 已启用，检查文件是否存在
            if not exist "!MODULE_PATH!\Kconfig" (
                echo %RED%✗ %%M: 已启用但 Kconfig 文件不存在%RESET%
                set /a ERRORS+=1
            ) else (
                echo %GREEN%✓ %%M: 已启用且文件存在%RESET%
            )
        ) else (
            echo %YELLOW%- %%M: 已禁用%RESET%
        )
    )
)

echo.
if %ERRORS% equ 0 (
    echo %GREEN%✓ 配置检查通过，未发现错误%RESET%
) else (
    echo %RED%✗ 发现 %ERRORS% 个错误%RESET%
)

goto :eof

REM ============================================================================
REM 显示帮助
REM ============================================================================
:show_help
echo.
echo %CYAN%==========================================%RESET%
echo   商业模块本地管理工具
echo %CYAN%==========================================%RESET%
echo.
echo 用法: proprietary_manage.bat ^<命令^> [模块名]
echo.
echo 命令:
echo   status          查看所有模块状态
echo   enable   ^<模块^>  启用指定模块
echo   disable  ^<模块^>  禁用指定模块
echo   enable-all      启用所有商业模块
echo   disable-all     禁用所有商业模块
echo   check           检查 Kconfig 配置
echo   help            显示此帮助信息
echo.
echo 可用模块:
for %%M in (%MODULES%) do (
    echo   - %%M
)
echo.
echo 示例:
echo   %0 status
echo   %0 enable event_system_pro
echo   %0 disable mesh_communication
echo   %0 enable-all
echo   %0 disable-all
echo.
echo 提示:
echo   - 修改配置后请重新运行 CMake
echo   - 模块文件应放置在 %PROPRIETARY_DIR%\^<模块名^>\ 目录下
echo.

goto :eof
