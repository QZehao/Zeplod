# Run ztest with IPC overlay (prj.conf + prj_native_sim).
$ErrorActionPreference = 'Stop'
$env:ZEPHYR_TEST_CONF = 'prj.conf;prj_native_sim.conf'
if (-not $env:ZEPHYR_TEST_BUILD_DIR) {
    $env:ZEPHYR_TEST_BUILD_DIR = 'build_tests_ipc'
}
& (Join-Path $PSScriptRoot 'run_tests.ps1') @args
