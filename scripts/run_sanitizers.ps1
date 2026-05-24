# Run ztest suite with host sanitizers (ASan/UBSan).
# Usage: .\scripts\run_sanitizers.ps1
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "setup_env.ps1")

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = if ($env:ZEPHYR_SAN_BUILD_DIR) { $env:ZEPHYR_SAN_BUILD_DIR } else { "build_sanitizers" }
$ConfFile = if ($env:ZEPHYR_TEST_CONF) { $env:ZEPHYR_TEST_CONF } else { "prj.conf" }
$Sanitizer = if ($env:ZEPHYR_SANITIZER) { $env:ZEPHYR_SANITIZER } else { "asan-ubsan" }

python (Join-Path $Root "scripts\preflight_host_tests.py")
if ($LASTEXITCODE -ne 0) {
    throw "Host preflight failed. See output above."
}

function Get-TestBoard {
    if ($env:ZEPHYR_TEST_BOARD) {
        return $env:ZEPHYR_TEST_BOARD
    }
    $boards = west boards 2>$null
    if ($boards -match "(?m)^native_sim$") {
        return "native_sim"
    }
    return "native_posix"
}

function Get-SanitizerFlags {
    param([string]$Name)
    switch ($Name) {
        "asan" { return "-fsanitize=address -fno-omit-frame-pointer -g" }
        "ubsan" { return "-fsanitize=undefined -fno-omit-frame-pointer -g" }
        "asan-ubsan" { return "-fsanitize=address,undefined -fno-omit-frame-pointer -g" }
        default { throw "Unsupported ZEPHYR_SANITIZER='$Name', use asan|ubsan|asan-ubsan" }
    }
}

$Board = Get-TestBoard
$BuildPath = Join-Path $Root $BuildDir
$SanFlags = Get-SanitizerFlags -Name $Sanitizer

$IsWindowsHost = $false
if (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) {
    $IsWindowsHost = [bool]$IsWindows
} elseif ($env:OS -eq "Windows_NT") {
    $IsWindowsHost = $true
}
if ($IsWindowsHost -and ($Board -eq "native_sim" -or $Board -eq "native_posix")) {
    throw "Board '$Board' requires Linux/WSL host for sanitizers."
}

Write-Host "Board: $Board, CONF_FILE: $ConfFile, Sanitizer: $Sanitizer, build-dir: $BuildPath"
Push-Location (Join-Path $Root "tests")
try {
    west build -b $Board . --build-dir $BuildPath -p always -- "-DCONF_FILE=$ConfFile" "-DCMAKE_C_FLAGS=$SanFlags" "-DCMAKE_EXE_LINKER_FLAGS=$SanFlags"
    west build -t run --build-dir $BuildPath
}
finally {
    Pop-Location
}
