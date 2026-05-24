# Run ztest suite (native_sim preferred, native_posix fallback).
# Loads Zephyr/west from zephyr_config.env via setup_env.ps1 (same as manual activation).
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'setup_env.ps1')

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = if ($env:ZEPHYR_TEST_BUILD_DIR) { $env:ZEPHYR_TEST_BUILD_DIR } else { 'build_tests' }
$ConfFile = if ($env:ZEPHYR_TEST_CONF) { $env:ZEPHYR_TEST_CONF } else { 'prj.conf' }

function Invoke-West {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Args
    )

    & west @Args
    if ($LASTEXITCODE -ne 0) {
        throw "west $($Args -join ' ') failed with exit code $LASTEXITCODE"
    }
}

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

$IsWindowsHost = $false
if (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) {
    $IsWindowsHost = [bool]$IsWindows
} elseif ($env:OS -eq 'Windows_NT') {
    $IsWindowsHost = $true
}

if ($IsWindowsHost -and ($Board -eq 'native_sim' -or $Board -eq 'native_posix')) {
    throw @"
Board '$Board' uses Zephyr POSIX architecture, which does not run on native Windows hosts.
Please run tests in Linux/WSL, or set ZEPHYR_TEST_BOARD to a non-POSIX board.
Example (WSL): ./scripts/run_tests.sh
"@
}

Write-Host "Board: $Board, CONF_FILE: $ConfFile, build-dir: $BuildPath"
Push-Location (Join-Path $Root 'tests')
try {
    Invoke-West -Args @('build', '-b', $Board, '.', '--build-dir', $BuildPath, '-p', 'always', '--', "-DCONF_FILE=$ConfFile")
    Invoke-West -Args @('build', '-t', 'run', '--build-dir', $BuildPath)
} finally {
    Pop-Location
}
