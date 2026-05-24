# Unified quality entrypoint.
# Usage: .\scripts\qa.ps1 -Mode all
param(
    [ValidateSet("test", "san", "twister", "all")]
    [string]$Mode = "all"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

function Run-Step([string]$ScriptPath) {
    & $ScriptPath
    if ($LASTEXITCODE -ne 0) {
        throw "$ScriptPath failed with exit code $LASTEXITCODE"
    }
}

switch ($Mode) {
    "test" { Run-Step (Join-Path $Root "scripts\run_tests.ps1") }
    "san" { Run-Step (Join-Path $Root "scripts\run_sanitizers.ps1") }
    "twister" { Run-Step (Join-Path $Root "scripts\run_twister.ps1") }
    "all" {
        Run-Step (Join-Path $Root "scripts\run_tests.ps1")
        Run-Step (Join-Path $Root "scripts\run_sanitizers.ps1")
        Run-Step (Join-Path $Root "scripts\run_twister.ps1")
    }
}
