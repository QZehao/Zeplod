# Run ztest with IPC + example modules overlay.
$ErrorActionPreference = 'Stop'
$env:ZEPHYR_TEST_CONF = 'prj.conf;prj_native_sim.conf;prj_ci_examples.conf'
if (-not $env:ZEPHYR_TEST_BUILD_DIR) {
    $env:ZEPHYR_TEST_BUILD_DIR = 'build_tests_full'
}
& (Join-Path $PSScriptRoot 'run_tests.ps1') @args
