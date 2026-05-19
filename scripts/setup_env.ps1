# Usage:
#   . .\scripts\setup_env.ps1    # activate in current PowerShell session (recommended)
#   .\scripts\setup_env.ps1      # same for $env: vars; venv PATH applies to this session
#
# Called automatically by scripts\run_tests.ps1 before west build.
$ErrorActionPreference = "Stop"

Write-Host "============================================"
Write-Host "Zephyr environment setup"
Write-Host "============================================"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$ConfigFile = Join-Path $ProjectRoot "zephyr_config.env"

if (-not (Test-Path $ConfigFile)) {
    Write-Host "Error: zephyr_config.env not found." -ForegroundColor Red
    Write-Host "Please copy zephyr_config.env.template to zephyr_config.env and edit paths."
    exit 1
}

Write-Host "Loading configuration from zephyr_config.env..."
$lines = Get-Content $ConfigFile
foreach ($line in $lines) {
    if ($line -match '^\s*#') { continue }
    if ($line -match '^\s*$') { continue }

    $eqPos = $line.IndexOf('=')
    if ($eqPos -gt 0) {
        $name = $line.Substring(0, $eqPos).Trim()
        $value = $line.Substring($eqPos + 1).Trim()
        if ($name) {
            Set-Variable -Name $name -Value $value -Scope Script
        }
    }
}

if ($VIRTUAL_ENV_PATH) {
    $VenvActivate = Join-Path $VIRTUAL_ENV_PATH "Scripts\Activate.ps1"
    if (Test-Path $VenvActivate) {
        . $VenvActivate
        Write-Host "Activated virtual environment: $VenvActivate" -ForegroundColor Green
    }
    else {
        Write-Host "Warning: virtual env activation script not found: $VenvActivate" -ForegroundColor Yellow
    }
}

if (-not $ZEPHYR_BASE) {
    Write-Host "Error: ZEPHYR_BASE is not set in config." -ForegroundColor Red
    exit 1
}

if (-not $ZEPHYR_SDK_INSTALL_DIR) {
    Write-Host "Error: ZEPHYR_SDK_INSTALL_DIR is not set in config." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $ZEPHYR_BASE)) {
    Write-Host "Error: ZEPHYR_BASE path does not exist: $ZEPHYR_BASE" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $ZEPHYR_SDK_INSTALL_DIR)) {
    Write-Host "Error: ZEPHYR_SDK_INSTALL_DIR path does not exist: $ZEPHYR_SDK_INSTALL_DIR" -ForegroundColor Red
    exit 1
}

$env:ZEPHYR_BASE = $ZEPHYR_BASE
$env:ZEPHYR_SDK_INSTALL_DIR = $ZEPHYR_SDK_INSTALL_DIR

[Environment]::SetEnvironmentVariable("ZEPHYR_BASE", $ZEPHYR_BASE, "User")
[Environment]::SetEnvironmentVariable("ZEPHYR_SDK_INSTALL_DIR", $ZEPHYR_SDK_INSTALL_DIR, "User")

$SdkBinPath = Join-Path $ZEPHYR_SDK_INSTALL_DIR "arm-zephyr-eabi\bin"
$SdkToolsPath = Join-Path $ZEPHYR_SDK_INSTALL_DIR "tools\bin"

if (Test-Path $SdkBinPath) {
    $env:PATH = "$SdkBinPath;$env:PATH"
}

if (Test-Path $SdkToolsPath) {
    $env:PATH = "$SdkToolsPath;$env:PATH"
}

$ZephyrEnvScript = Join-Path $ZEPHYR_BASE "scripts\env.bat"
if (Test-Path $ZephyrEnvScript) {
    Write-Host "Running Zephyr environment script..."
    & $ZephyrEnvScript
}

Write-Host "============================================"
Write-Host "Environment configured successfully." -ForegroundColor Green
Write-Host "============================================"
Write-Host "ZEPHYR_BASE=$ZEPHYR_BASE"
Write-Host "ZEPHYR_SDK_INSTALL_DIR=$ZEPHYR_SDK_INSTALL_DIR"
Write-Host "============================================"
Write-Host ""
Write-Host "You can now build:"
Write-Host "  west build -b $DEFAULT_BOARD -d $BUILD_DIR ."
Write-Host ""