# Build and run framework or framework+app project in QEMU (Windows).
#
# Usage:
#   .\scripts\run_qemu.ps1
#   .\scripts\run_qemu.ps1 -Board qemu_cortex_m3
#   .\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildOnly
#   .\scripts\run_qemu.ps1 -ListBoards
#   .\scripts\run_qemu.ps1 -Target app      # force app wrapper build
#   .\scripts\run_qemu.ps1 -Target framework

param(
    [string] $Board = "qemu_riscv32",
    [string] $BuildDir = "",
    [switch] $BuildOnly,
    [switch] $ListBoards,
    [switch] $NoPristine,
    [ValidateSet("auto", "framework", "app")]
    [string] $Target = "auto"
)

$ErrorActionPreference = "Stop"

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

. (Join-Path $PSScriptRoot "project_layout.ps1")
. (Join-Path $PSScriptRoot "setup_env.ps1")

Set-ZephyrConsoleUtf8

$Layout = Initialize-ZephyrProjectLayout -ScriptsDir $PSScriptRoot -Target $(if ($Target -eq "framework") { "framework" } else { "auto" })

if ($Target -eq "framework") {
    if ($Layout.Mode -eq "app") {
        $Root = $Layout.AppRoot
        $WestSource = "framework"
    } else {
        $Root = $Layout.FrameworkRoot
        $WestSource = "."
    }
} else {
    $Root = $Layout.WorkRoot
    $WestSource = "."
}

if ($Target -eq "framework" -and $Layout.Mode -eq "app") {
    $ConfFile = "prj.conf;prj_qemu.conf"
} else {
    $ConfFile = Get-ZephyrQemuConfFile -Layout $Layout
}
if ($BuildDir -eq "") {
    $prefix = if ($Layout.Mode -eq "app" -and $WestSource -eq ".") { "build_qemu_app" } else { "build_qemu" }
    $BuildDir = "${prefix}_$($Board -replace '[^a-zA-Z0-9_]', '_')"
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
    # Must stream stdout/stderr directly — buffering (2>&1) hides QEMU serial output until exit.
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        & west @Args
        if ($LASTEXITCODE -ne 0) {
            throw "west $($Args -join ' ') failed (exit $LASTEXITCODE)"
        }
    }
    finally {
        $ErrorActionPreference = $prevEap
    }
}

$QemuDir = Resolve-QemuBinPath
$env:QEMU_BIN_PATH = $QemuDir
$env:PATH = "$QemuDir;$env:PATH"

Write-Host "Mode:      $($Layout.Mode)"
Write-Host "Board:     $Board"
Write-Host "Source:    $WestSource"
Write-Host "Build dir: $BuildPath"
Write-Host "QEMU:      $QemuDir"
if ($ConfFile) {
    Write-Host "CONF:      $ConfFile"
} else {
    Write-Host "CONF:      (CMakeLists default)"
}

Push-Location $Root
try {
    $westExtra = @()
    if ($ConfFile) {
        $westExtra = @("--", "-DCONF_FILE=$ConfFile")
    }

    if ($NoPristine) {
        $westArgs = @("build", "-b", $Board, "-d", $BuildPath, $WestSource) + $westExtra
    }
    else {
        $westArgs = @("build", "-b", $Board, "-d", $BuildPath, $WestSource, "-p", "always") + $westExtra
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
