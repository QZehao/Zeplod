@echo off
REM Windows 批量构建脚本（委托 PowerShell，支持 framework/app 布局）
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_all.ps1" %*
exit /b %ERRORLEVEL%
