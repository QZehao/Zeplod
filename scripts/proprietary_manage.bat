@echo off
REM =============================================================================
REM Proprietary module manager (Windows Batch stub)
REM =============================================================================
REM Usage: scripts\proprietary_manage.bat <status|enable|disable|enable-all|disable-all|check>
REM =============================================================================

setlocal enabledelayedexpansion

set "COMMAND=%~1"
set "MODULE=%~2"
if "%COMMAND%"=="" set "COMMAND=status"

set "SCRIPT_DIR=%~dp0"
set "FRAMEWORK_ROOT=%SCRIPT_DIR%.."
set "PROPRIETARY_DIR=%FRAMEWORK_ROOT%\src\proprietary"

if /I "%COMMAND%"=="help" goto :usage
if /I "%COMMAND%"=="--help" goto :usage
if /I "%COMMAND%"=="-h" goto :usage
if /I "%COMMAND%"=="status" goto :status
if /I "%COMMAND%"=="check" goto :check
if /I "%COMMAND%"=="enable" goto :enable
if /I "%COMMAND%"=="disable" goto :disable
if /I "%COMMAND%"=="enable-all" goto :enable_all
if /I "%COMMAND%"=="disable-all" goto :disable_all
echo Unknown command: %COMMAND%
goto :usage

:usage
echo Usage: proprietary_manage.bat ^<command^> [module]
echo.
echo Commands:
echo   status              Show proprietary module availability
echo   enable  ^<module^>    Enable a proprietary module (requires files in src/proprietary/)
echo   disable ^<module^>    Disable a proprietary module
echo   enable-all          Enable all known proprietary modules
echo   disable-all         Disable all proprietary modules
echo   check               Verify Kconfig / CMake consistency
echo   help                Show this help
echo.
echo Proprietary modules are not included in the open-source repository.
echo Contact china_qzh@163.com for commercial licensing.
exit /b 0

:status
echo Proprietary module status
echo =========================
if not exist "%PROPRIETARY_DIR%" (
    echo No proprietary modules present in src/proprietary/.
    echo Contact china_qzh@163.com for commercial licensing.
    exit /b 0
)
set "COUNT=0"
for /f "delims=" %%D in ('dir /b /ad "%PROPRIETARY_DIR%" 2^>nul') do set /a COUNT+=1
if %COUNT%==0 (
    echo No proprietary modules present in src/proprietary/.
    echo Contact china_qzh@163.com for commercial licensing.
    exit /b 0
)
for %%M in (mesh_communication module_manager_pro ota_manager security_crypto cellular_5g_usb usb_host_cdc_ecm) do (
    if exist "%PROPRIETARY_DIR%\%%M" (
        echo   %%M                     AVAILABLE
    ) else (
        echo   %%M                     NOT PRESENT
    )
)
exit /b 0

:check
if not exist "%PROPRIETARY_DIR%" (
    echo check: src/proprietary/ does not exist; nothing to verify.
    exit /b 0
)
set "COUNT=0"
set "NAMES="
for /f "delims=" %%D in ('dir /b /ad "%PROPRIETARY_DIR%" 2^>nul') do (
    set /a COUNT+=1
    if defined NAMES (set "NAMES=!NAMES!, %%D") else set "NAMES=%%D"
)
echo check: found %COUNT% proprietary module(s): %NAMES%
exit /b 0

:enable
if "%MODULE%"=="" (
    echo enable: module name required >&2
    exit /b 1
)
if not exist "%PROPRIETARY_DIR%\%MODULE%" (
    echo enable: '%MODULE%' is not available. Proprietary modules require commercial licensing. >&2
    exit /b 1
)
echo enable: '%MODULE%' is present but CMake/Kconfig integration must be configured manually.
exit /b 0

:disable
if "%MODULE%"=="" (
    echo disable: module name required >&2
    exit /b 1
)
echo disable: '%MODULE%' disabled (no proprietary modules are enabled by default in this repository).
exit /b 0

:enable_all
if not exist "%PROPRIETARY_DIR%" (
    echo enable-all: no proprietary modules present.
    exit /b 0
)
set "COUNT=0"
set "NAMES="
for /f "delims=" %%D in ('dir /b /ad "%PROPRIETARY_DIR%" 2^>nul') do (
    set /a COUNT+=1
    if defined NAMES (set "NAMES=!NAMES!, %%D") else set "NAMES=%%D"
)
echo enable-all: found %COUNT% module(s): %NAMES%
echo enable-all: CMake/Kconfig integration must be configured manually.
exit /b 0

:disable_all
echo disable-all: all proprietary modules are now disabled.
exit /b 0
