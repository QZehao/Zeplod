# 固件打包脚本
# 用于发布时打包构建产物

param(
    [string]$Version = "",
    [string]$BuildDir = "build",
    [string]$OutputDir = "release",
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_ROOT = Split-Path -Parent $SCRIPT_DIR

Write-Host "============================================"
Write-Host "Zephyr 固件打包工具"
Write-Host "============================================"
Write-Host ""

# 帮助
if ($Help) {
    Write-Host "用法：.\package_release.ps1 [选项]"
    Write-Host ""
    Write-Host "选项:"
    Write-Host "  -Version VER     版本号"
    Write-Host "  -BuildDir DIR    构建目录（默认：build）"
    Write-Host "  -OutputDir DIR   输出目录（默认：release）"
    Write-Host "  -Help            显示帮助"
    exit 0
}

# 读取 APP_VERSION 文件
$appVersion = "unknown"
$appVersionFile = Join-Path $PROJECT_ROOT "APP_VERSION"
if (Test-Path $appVersionFile) {
    $appVersion = Get-Content $appVersionFile -Raw | ForEach-Object { $_.Trim() }
    if ([string]::IsNullOrEmpty($appVersion)) {
        $appVersion = "unknown"
    }
}

# 如果没有指定版本号，优先使用 APP_VERSION，然后尝试从 git 获取
if ([string]::IsNullOrEmpty($Version) -or $Version -eq "unknown") {
    $Version = $appVersion
    try {
        $gitVersion = git describe --tags --always --dirty 2>$null
        if (![string]::IsNullOrEmpty($gitVersion)) {
            $Version = $gitVersion
        }
    } catch {
        # 使用 APP_VERSION
    }
}

$buildDate = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$buildHost = $env:COMPUTERNAME

# 创建版本信息文件
Write-Host "正在创建版本信息..."

$versionContent = @"
================================================================================
Zephyr Event-Driven Project Template - Release Package
================================================================================

版本信息
--------
  APP 版本：   $appVersion
  Git 版本：   $Version
  编译时间：   $buildDate
  编译主机：   $buildHost

文件列表
--------
$(Get-ChildItem -Path $OutputDir -Include *.bin,*.elf,*.hex,*.map -File | ForEach-Object { "  $($_.Name)  ($('{0:N0}' -f $_.Length) bytes)" })

================================================================================
@endContent

$versionContent | Out-File -FilePath (Join-Path $OutputDir "release_info.txt") -Encoding UTF8
Write-Host "  ✓ release_info.txt"

# 复制 README 和许可证
if (Test-Path (Join-Path $PROJECT_ROOT "README.md")) {
    Copy-Item (Join-Path $PROJECT_ROOT "README.md") -Destination $OutputDir -Force
    Write-Host "  ✓ README.md"
}

if (Test-Path (Join-Path $PROJECT_ROOT "LICENSE")) {
    Copy-Item (Join-Path $PROJECT_ROOT "LICENSE") -Destination $OutputDir -Force
    Write-Host "  ✓ LICENSE"
}

# 创建压缩包
Write-Host ""
Write-Host "正在创建压缩包..."

$archiveName = "zephyr_template_${Version}"
$outputPath = Join-Path $OutputDir $archiveName

# 使用 PowerShell 内置的 Compress-Archive
try {
    $tempZip = Join-Path $OutputDir "${archiveName}.zip"
    Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $tempZip -Force
    Write-Host "  ✓ ${archiveName}.zip"
} catch {
    Write-Host "  ⚠ 创建压缩包失败：$_"
}

Write-Host ""
Write-Host "============================================"
Write-Host "打包完成"
Write-Host "============================================"
Write-Host "输出目录：$OutputDir"
Write-Host ""
Write-Host "文件列表:"
Get-ChildItem -Path $OutputDir | Format-Table -AutoSize
