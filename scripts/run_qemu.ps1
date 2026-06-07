# Build and run zeplod in QEMU (Windows).
#
# Usage:
#   .\scripts\run_qemu.ps1
#   .\scripts\run_qemu.ps1 -Board qemu_cortex_m3
#   .\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildOnly
#   .\scripts\run_qemu.ps1 -ListBoards

param(
    [string] $Board = "qemu_riscv32",
    [string] $BuildDir = "",
    [switch] $BuildOnly,
    [switch] $ListBoards,
    [switch] $NoPristine
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "setup_env.ps1")

$Root = Split-Path -Parent $PSScriptRoot

$QemuBoards = @(
    "qemu_riscv32",
    "qemu_riscv64",
    "qemu_cortex_m3",
    "qemu_cortex_m0",
    "qemu_x86",
    "qemu_x86_64",
    "qemu_x86_tiny",
    "qemu_cortex_a9",
    "qemu_cortex_a53",
    "qemu_cortex_r5",
    "qemu_riscv32e",
    "qemu_riscv32_xip",
    "qemu_malta",
    "qemu_leon3",
    "qemu_rx",
    "qemu_xtensa_dc233c",
    "qemu_arc_qemu_arc_em",
    "qemu_x86_lakemont"
)

$QemuSmpBoards = @(
    "qemu_riscv32/qemu_virt_riscv32/smp",
    "qemu_riscv64/qemu_virt_riscv64/smp",
    "qemu_cortex_a53/qemu_cortex_a53/smp",
    "qemu_x86_64 (2 CPUs, SMP in defconfig)"
)

if ($ListBoards) {
    Write-Host "QEMU boards (see boards/qemu_*.overlay):"
    foreach ($name in $QemuBoards) {
        Write-Host "  $name"
    }
    Write-Host ""
    Write-Host "SMP variants (multi-core, see docs/zh-CN/10-环境与构建/14-QEMU仿真运行指南.md):"
    foreach ($name in $QemuSmpBoards) {
        Write-Host "  $name"
    }
    Write-Host "  qemu_kvm_arm64 (Linux KVM only, not Windows)"
    return
}

if ($Board -eq "qemu_kvm_arm64") {
    throw "qemu_kvm_arm64 requires Linux KVM and cannot run on native Windows."
}

$ConfFile = "prj.conf;prj_qemu.conf"
if ($BuildDir -eq "") {
    $BuildDir = "build_qemu_$($Board -replace '[^a-zA-Z0-9_]', '_')"
}
$BuildPath = Join-Path $Root $BuildDir

function Resolve-QemuBinPath {
    if ($env:QEMU_BIN_PATH -and (Test-Path $env:QEMU_BIN_PATH)) {
        return $env:QEMU_BIN_PATH
    }
    $candidates = @(
        "C:\Program Files\qemu",
        "${env:ProgramFiles}\qemu",
        "${env:ProgramFiles(x86)}\qemu"
    )
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir "qemu-system-riscv32.exe")) {
            return $dir
        }
    }
    throw "QEMU not found. Install: winget install SoftwareFreedomConservancy.QEMU"
}

function Invoke-West {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Args
    )

    # west writes progress to stderr; avoid PowerShell treating it as a terminating error.
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        $lines = & west @Args 2>&1
        $exit = $LASTEXITCODE
        foreach ($line in $lines) {
            if ($line -is [System.Management.Automation.ErrorRecord]) {
                Write-Host $line.Exception.Message
            }
            else {
                Write-Host $line
            }
        }
        if ($exit -ne 0) {
            throw "west $($Args -join ' ') failed (exit $exit)"
        }
    }
    finally {
        $ErrorActionPreference = $prevEap
    }
}

$QemuDir = Resolve-QemuBinPath
$env:QEMU_BIN_PATH = $QemuDir
$env:PATH = "$QemuDir;$env:PATH"

Write-Host "Board:     $Board"
Write-Host "Build dir: $BuildPath"
Write-Host "QEMU:      $QemuDir"
Write-Host "CONF:      $ConfFile"

Push-Location $Root
try {
    if ($NoPristine) {
        $westArgs = @(
            "build", "-b", $Board, "-d", $BuildPath, ".",
            "--", "-DCONF_FILE=$ConfFile"
        )
    }
    else {
        $westArgs = @(
            "build", "-b", $Board, "-d", $BuildPath, ".", "-p", "always",
            "--", "-DCONF_FILE=$ConfFile"
        )
    }

    Invoke-West -Args $westArgs

    if ($BuildOnly) {
        Write-Host "Build OK. Run: west build -t run --build-dir $BuildPath"
        return
    }

    Write-Host "Starting QEMU (exit: Ctrl+A then X)..."
    Invoke-West -Args @("build", "-t", "run", "--build-dir", $BuildPath)
}
finally {
    Pop-Location
}
