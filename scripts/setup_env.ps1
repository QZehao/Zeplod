#!/usr/bin/env pwsh
# =============================================================================
# Zephyr 环境设置脚本 (Windows PowerShell)
# =============================================================================
# 用法：.\scripts\setup_env.ps1
# =============================================================================

$ErrorActionPreference = "Stop"

Write-Host "============================================"
Write-Host "Zephyr 环境设置"
Write-Host "============================================"

# 获取脚本目录
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ConfigFile = Join-Path $ProjectRoot "zephyr_config.env"

# 检查配置文件是否存在
if (-not (Test-Path $ConfigFile)) {
    Write-Host "错误：找不到 zephyr_config.env！" -ForegroundColor Red
    Write-Host "请复制 zephyr_config.env.template 到 zephyr_config.env 并编辑路径。"
    exit 1
}

# 加载配置
Write-Host "正在从 zephyr_config.env 加载配置..."
Get-Content $ConfigFile | ForEach-Object {
    if ($_ -match '^\s*#' -or $_ -match '^\s*$') {
        return
    }
    if ($_ -match '^(.*?)=(.*)$') {
        $name = $matches[1]
        $value = $matches[2]
        Set-Variable -Name $name -Value $value -Scope Script
    }
}

# 验证路径
if (-not $ZEPHYR_BASE) {
    Write-Host "错误：配置中未设置 ZEPHYR_BASE！" -ForegroundColor Red
    exit 1
}

if (-not $ZEPHYR_SDK_INSTALL_DIR) {
    Write-Host "错误：配置中未设置 ZEPHYR_SDK_INSTALL_DIR！" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $ZEPHYR_BASE)) {
    Write-Host "错误：ZEPHYR_BASE 路径不存在：$ZEPHYR_BASE" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $ZEPHYR_SDK_INSTALL_DIR)) {
    Write-Host "错误：ZEPHYR_SDK_INSTALL_DIR 路径不存在：$ZEPHYR_SDK_INSTALL_DIR" -ForegroundColor Red
    exit 1
}

# 设置环境变量（当前会话）
$env:ZEPHYR_BASE = $ZEPHYR_BASE
$env:ZEPHYR_SDK_INSTALL_DIR = $ZEPHYR_SDK_INSTALL_DIR

# 设置环境变量（用户级别）
[Environment]::SetEnvironmentVariable("ZEPHYR_BASE", $ZEPHYR_BASE, "User")
[Environment]::SetEnvironmentVariable("ZEPHYR_SDK_INSTALL_DIR", $ZEPHYR_SDK_INSTALL_DIR, "User")

# 添加 Zephyr 工具到 PATH
$SdkBinPath = Join-Path $ZEPHYR_SDK_INSTALL_DIR "arm-zephyr-eabi\bin"
$SdkToolsPath = Join-Path $ZEPHYR_SDK_INSTALL_DIR "tools\bin"

if (Test-Path $SdkBinPath) {
    $env:PATH = "$SdkBinPath;$env:PATH"
}

if (Test-Path $SdkToolsPath) {
    $env:PATH = "$SdkToolsPath;$env:PATH"
}

# 运行 Zephyr 环境设置脚本（如果存在）
$ZephyrEnvScript = Join-Path $ZEPHYR_BASE "scripts\env.bat"
if (Test-Path $ZephyrEnvScript) {
    Write-Host "正在运行 Zephyr 环境脚本..."
    & $ZephyrEnvScript
}

Write-Host "============================================"
Write-Host "环境配置成功！" -ForegroundColor Green
Write-Host "============================================"
Write-Host "ZEPHYR_BASE=$ZEPHYR_BASE"
Write-Host "ZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR"
Write-Host "============================================"
Write-Host ""
Write-Host "现在可以构建项目：" -ForegroundColor Green
Write-Host "  west build -b $DEFAULT_BOARD ."
Write-Host ""
