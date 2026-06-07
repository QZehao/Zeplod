# Unified quality entrypoint.
# Usage: .\scripts\qa.ps1 -Mode all
param(
    [ValidateSet("test", "san", "twister", "all")]
    [string]$Mode = "all"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "project_layout.ps1")
$Layout = Initialize-ZephyrProjectLayout -ScriptsDir $PSScriptRoot
$ScriptsRoot = $Layout.ScriptsRoot

function Run-Step([string]$ScriptPath) {
    & $ScriptPath
    if ($LASTEXITCODE -ne 0) {
        throw "$ScriptPath failed with exit code $LASTEXITCODE"
    }
}

switch ($Mode) {
    "test" { Run-Step (Join-Path $ScriptsRoot "run_tests.ps1") }
    "san" { Run-Step (Join-Path $ScriptsRoot "run_sanitizers.ps1") }
    "twister" { Run-Step (Join-Path $ScriptsRoot "run_twister.ps1") }
    "all" {
        Run-Step (Join-Path $ScriptsRoot "run_tests.ps1")
        Run-Step (Join-Path $ScriptsRoot "run_sanitizers.ps1")
        Run-Step (Join-Path $ScriptsRoot "run_twister.ps1")
    }
}
