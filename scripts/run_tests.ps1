# Run ztest suite.
# Linux/WSL: native_sim preferred, native_posix fallback.
# Windows native: qemu_riscv32 (POSIX boards are not supported without WSL).
# Loads Zephyr/west from zephyr_config.env via setup_env.ps1 (same as manual activation).
# Supports framework-only repos and app repos with framework/ submodule.
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'project_layout.ps1')
. (Join-Path $PSScriptRoot 'setup_env.ps1')

$IsWindowsHost = $false
if (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue) {
    $IsWindowsHost = [bool]$IsWindows
} elseif ($env:OS -eq 'Windows_NT') {
    $IsWindowsHost = $true
}

# Preflight runs before board selection; set QEMU board early on native Windows.
if ($IsWindowsHost -and -not $env:ZEPHYR_TEST_BOARD) {
    $env:ZEPHYR_TEST_BOARD = 'qemu_riscv32'
}

$Layout = Initialize-ZephyrProjectLayout -ScriptsDir $PSScriptRoot
$Root = $Layout.WorkRoot
$TestsDir = $Layout.TestsDir
$BuildDir = if ($env:ZEPHYR_TEST_BUILD_DIR) { $env:ZEPHYR_TEST_BUILD_DIR } else { 'build_tests' }
$ConfFile = if ($env:ZEPHYR_TEST_CONF) { $env:ZEPHYR_TEST_CONF } else { 'prj.conf;prj_test_extensions.conf' }

python (Join-Path $Layout.ScriptsRoot "preflight_host_tests.py")
if ($LASTEXITCODE -ne 0) {
    throw "Host preflight failed. See output above."
}

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
    if ($IsWindowsHost) {
        return 'qemu_riscv32'
    }
    $boards = west boards 2>$null
    if ($boards -match '(?m)^native_sim$') {
        return 'native_sim'
    }
    return 'native_posix'
}

$Board = Get-TestBoard

if ($IsWindowsHost -and ($Board -eq 'native_sim' -or $Board -eq 'native_posix')) {
    throw @"
Board '$Board' uses Zephyr POSIX architecture, which does not run on native Windows hosts.
Please run tests in Linux/WSL, or set ZEPHYR_TEST_BOARD to a non-POSIX board (e.g. qemu_riscv32).
Example (WSL): ./scripts/run_tests.sh
"@
}

if (-not $env:ZEPHYR_TEST_BUILD_DIR -and $Board -match '^qemu_') {
    $suffix = $Board -replace '[^a-zA-Z0-9_]', '_'
    $BuildDir = "build_tests_qemu_$suffix"
}

$BuildPath = Join-Path $Root $BuildDir

Write-Host "Mode: $($Layout.Mode), board: $Board, CONF_FILE: $ConfFile, build-dir: $BuildPath"
Push-Location $TestsDir
try {
    Invoke-West -Args @('build', '-b', $Board, '.', '--build-dir', $BuildPath, '-p', 'always', '--', "-DCONF_FILE=$ConfFile")
    Set-ZephyrConsoleUtf8
    Invoke-West -Args @('build', '-t', 'run', '--build-dir', $BuildPath)
} finally {
    Pop-Location
}
