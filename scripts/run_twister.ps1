# Run Zephyr twister for tests/ with host simulation boards.
# Usage: .\scripts\run_twister.ps1
param(
    [string]$Platform = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "setup_env.ps1")

$Root = Split-Path -Parent $PSScriptRoot
if (-not $Platform) {
    $Platform = if ($env:ZEPHYR_TWISTER_PLATFORM) { $env:ZEPHYR_TWISTER_PLATFORM } else { "native_sim" }
}
if (-not $OutDir) {
    $OutDir = if ($env:ZEPHYR_TWISTER_OUT_DIR) { $env:ZEPHYR_TWISTER_OUT_DIR } else { "twister-out" }
}

python (Join-Path $Root "scripts\preflight_host_tests.py")
if ($LASTEXITCODE -ne 0) {
    throw "Host preflight failed. See output above."
}

$IsWindowsHost = $false
if (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) {
    $IsWindowsHost = [bool]$IsWindows
} elseif ($env:OS -eq "Windows_NT") {
    $IsWindowsHost = $true
}
if ($IsWindowsHost -and ($Platform -eq "native_sim" -or $Platform -eq "native_posix")) {
    throw "Platform '$Platform' requires Linux/WSL host for twister."
}

west twister -h | Out-Null
Push-Location $Root
try {
    west twister -T tests -p $Platform -O $OutDir --inline-logs
}
finally {
    Pop-Location
}
