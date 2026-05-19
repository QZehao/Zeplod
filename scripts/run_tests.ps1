# Run ztest suite (native_sim preferred, native_posix fallback).
# Loads Zephyr/west from zephyr_config.env via setup_env.ps1 (same as manual activation).
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'setup_env.ps1')

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = if ($env:ZEPHYR_TEST_BUILD_DIR) { $env:ZEPHYR_TEST_BUILD_DIR } else { 'build_tests' }
$ConfFile = if ($env:ZEPHYR_TEST_CONF) { $env:ZEPHYR_TEST_CONF } else { 'prj.conf' }

function Get-TestBoard {
    if ($env:ZEPHYR_TEST_BOARD) {
        return $env:ZEPHYR_TEST_BOARD
    }
    $boards = west boards 2>$null
    if ($boards -match '(?m)^native_sim$') {
        return 'native_sim'
    }
    return 'native_posix'
}

$Board = Get-TestBoard
$BuildPath = Join-Path $Root $BuildDir

Write-Host "Board: $Board, CONF_FILE: $ConfFile, build-dir: $BuildPath"
Push-Location (Join-Path $Root 'tests')
try {
    west build -b $Board . --build-dir $BuildPath -p always -- "-DCONF_FILE=$ConfFile"
    west build -t run --build-dir $BuildPath
} finally {
    Pop-Location
}
