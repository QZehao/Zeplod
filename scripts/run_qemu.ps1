# Build and run framework, app, or ztest unit tests in QEMU (Windows).
#
# Usage (unchanged — main app / framework):
#   .\scripts\run_qemu.ps1
#   .\scripts\run_qemu.ps1 -Board qemu_cortex_m3
#   .\scripts\run_qemu.ps1 -Board qemu_riscv32 -BuildOnly
#   .\scripts\run_qemu.ps1 -ListBoards
#   .\scripts\run_qemu.ps1 -Target app
#   .\scripts\run_qemu.ps1 -Target framework
#
# Unit tests + coverage (optional):
#   .\scripts\run_qemu.ps1 -Tests
#   .\scripts\run_qemu.ps1 -Tests -Coverage
#   .\scripts\run_qemu.ps1 -Tests -Coverage -CoverageMin 95
#   .\scripts\run_qemu.ps1 -Tests -Coverage -ReportOnly   # reuse existing log

param(
    [string] $Board = "qemu_riscv32",
    [string] $BuildDir = "",
    [switch] $BuildOnly,
    [switch] $ListBoards,
    [switch] $NoPristine,
    [switch] $Tests,
    [switch] $Coverage,
    [int] $CoverageMin = 0,
    [string] $CoverageFilter = "src/core/",
    [string] $TestConf = "",
    [string] $CoverageLog = "",
    [switch] $ReportOnly,
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

if ($Coverage -and -not $Tests) {
    throw "-Coverage requires -Tests."
}

. (Join-Path $PSScriptRoot "project_layout.ps1")
. (Join-Path $PSScriptRoot "setup_env.ps1")

Set-ZephyrConsoleUtf8

$Layout = Initialize-ZephyrProjectLayout -ScriptsDir $PSScriptRoot -Target $(if ($Target -eq "framework") { "framework" } else { "auto" })
$FrameworkRoot = $Layout.FrameworkRoot

if ($Tests) {
    $env:ZEPHYR_TEST_BOARD = $Board
    python (Join-Path $Layout.ScriptsRoot "preflight_host_tests.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Host preflight failed. See output above."
    }
}

if ($Tests) {
    $Root = $FrameworkRoot
    $WestSource = "tests"
    if ($TestConf -eq "") {
        if ($Coverage) {
            $TestConf = "prj.conf;prj_test_extensions.conf;prj_qemu_coverage.conf;prj_block_overflow.conf"
        } else {
            $TestConf = "prj.conf;prj_test_extensions.conf"
        }
    }
    $ConfFile = $TestConf
} elseif ($Target -eq "framework") {
    if ($Layout.Mode -eq "app") {
        $Root = $Layout.AppRoot
        $WestSource = "framework"
    } else {
        $Root = $Layout.FrameworkRoot
        $WestSource = "."
    }
    if ($Layout.Mode -eq "app") {
        $ConfFile = "prj.conf;prj_qemu.conf"
    } else {
        $ConfFile = Get-ZephyrQemuConfFile -Layout $Layout
    }
} else {
    $Root = $Layout.WorkRoot
    $WestSource = "."
    $ConfFile = Get-ZephyrQemuConfFile -Layout $Layout
}

if ($BuildDir -eq "") {
    if ($Tests) {
        $suffix = $Board -replace '[^a-zA-Z0-9_]', '_'
        if ($Coverage) {
            $BuildDir = "build_tests_qemu_cov_$suffix"
        } else {
            $BuildDir = "build_tests_qemu_$suffix"
        }
    } else {
        $prefix = if ($Layout.Mode -eq "app" -and $WestSource -eq ".") { "build_qemu_app" } else { "build_qemu" }
        $BuildDir = "${prefix}_$($Board -replace '[^a-zA-Z0-9_]', '_')"
    }
}
$BuildPath = Join-Path $Root $BuildDir

if ($CoverageLog -eq "") {
    $CoverageLog = Join-Path $Root "coverage_qemu_$($Board -replace '[^a-zA-Z0-9_]', '_').log"
}

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

function Resolve-ZephyrGcovExecutable {
    $sdk = $env:ZEPHYR_SDK_INSTALL_DIR
    if (-not $sdk) {
        throw "ZEPHYR_SDK_INSTALL_DIR is not set. Run setup_env.ps1 first."
    }
    $candidates = @(
        (Join-Path $sdk "riscv64-zephyr-elf\bin\riscv64-zephyr-elf-gcov.exe"),
        (Join-Path $sdk "arm-zephyr-eabi\bin\arm-zephyr-eabi-gcov.exe")
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            return ($path -replace '\\', '/')
        }
    }
    throw "No Zephyr SDK gcov found under $sdk"
}

function Invoke-West {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Args
    )

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

function Invoke-WestRunWithLog {
    param(
        [string] $OutBuildPath,
        [string] $LogPath
    )

    if (Test-Path $LogPath) {
        Remove-Item $LogPath -Force
    }

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & west build -t run --build-dir $OutBuildPath 2>&1 | ForEach-Object {
            $_ | Out-File -FilePath $LogPath -Append -Encoding utf8
            Write-Host $_
        }
        if ($LASTEXITCODE -ne 0) {
            throw "west build -t run failed (exit $LASTEXITCODE). See $LogPath"
        }
    }
    finally {
        $ErrorActionPreference = $prevEap
    }
}

function Import-GcovDumpFromLog {
    param(
        [string] $LogPath
    )

    if (-not (Test-Path $LogPath)) {
        throw "Coverage log not found: $LogPath"
    }

    $genScript = Join-Path $env:ZEPHYR_BASE "scripts\gen_gcov_files.py"
    if (-not (Test-Path $genScript)) {
        throw "Zephyr gen_gcov_files.py not found at $genScript"
    }

    # gen_gcov_files.py opens with default locale encoding; normalize to UTF-8 first.
    $bytes = [System.IO.File]::ReadAllBytes($LogPath)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        $text = [System.Text.Encoding]::Unicode.GetString($bytes)
    } elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
    } else {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    }
    $utf8Log = "$LogPath.utf8"
    [System.IO.File]::WriteAllText($utf8Log, $text, [System.Text.UTF8Encoding]::new($false))

    python $genScript -i $utf8Log
    if ($LASTEXITCODE -ne 0) {
        throw "gen_gcov_files.py failed (exit $LASTEXITCODE)"
    }
}

function Invoke-CoverageReport {
    param(
        [string] $RepoRoot,
        [string] $ObjectDir,
        [string] $Filter,
        [int] $MinLine,
        [string] $GcovExe
    )

    if (-not (Get-Command gcovr -ErrorAction SilentlyContinue)) {
        pip install gcovr
    }

    $reportBase = "coverage-qemu-core"
    $htmlOut = "$reportBase.html"
    $jsonOut = "$reportBase-summary.json"
    $gcovArgs = @(
        "-r", $RepoRoot,
        "--gcov-executable", $GcovExe,
        "--object-directory", (Join-Path $ObjectDir "CMakeFiles/app.dir"),
        "--filter", $Filter,
        "--merge-mode-functions=merge-use-line-0",
        "-j", "1",
        "--print-summary",
        "--html-details", $htmlOut,
        "--json-summary-pretty",
        "--output", $jsonOut
    )
    if ($MinLine -gt 0) {
        $gcovArgs += @("--fail-under-line", "$MinLine")
    }

    Push-Location $RepoRoot
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & gcovr @gcovArgs
        if ($LASTEXITCODE -ne 0) {
            throw "gcovr failed (exit $LASTEXITCODE); line coverage below $MinLine% for '$Filter'"
        }
    }
    finally {
        $ErrorActionPreference = $prevEap
        Pop-Location
    }

    $summaryPath = Join-Path $RepoRoot "$reportBase-summary.json"
    if (Test-Path $summaryPath) {
        $json = Get-Content $summaryPath -Raw | ConvertFrom-Json
        Write-Host ""
        Write-Host "Coverage ($Filter): $([math]::Round($json.line_percent, 2))% lines"
        Write-Host "HTML: $(Join-Path $RepoRoot "$reportBase.html")"
    }
}

$QemuDir = Resolve-QemuBinPath
$env:QEMU_BIN_PATH = $QemuDir
$env:PATH = "$QemuDir;$env:PATH"

Write-Host "Mode:      $($Layout.Mode)"
Write-Host "Board:     $Board"
if ($Tests) {
    Write-Host "Target:    ztest (tests/)"
} else {
    Write-Host "Source:    $WestSource"
}
Write-Host "Build dir: $BuildPath"
Write-Host "QEMU:      $QemuDir"
if ($ConfFile) {
    Write-Host "CONF:      $ConfFile"
} else {
    Write-Host "CONF:      (CMakeLists default)"
}
if ($Coverage) {
    Write-Host "Coverage:  filter=$CoverageFilter min=${CoverageMin}% log=$CoverageLog"
}

Push-Location $Root
try {
    if ($Tests) {
        if (-not $ReportOnly) {
            $westExtra = @("--", "-DCONF_FILE=$ConfFile")
            if ($Coverage) {
                $westExtra += @("-DCONFIG_COVERAGE=y", "-DCONFIG_COVERAGE_DUMP=y")
            }

            if ($NoPristine) {
                $westArgs = @("build", "-b", $Board, "-d", $BuildPath, "tests") + $westExtra
            } else {
                $westArgs = @("build", "-b", $Board, "-d", $BuildPath, "tests", "-p", "always") + $westExtra
            }
            Invoke-West -Args $westArgs

            if ($BuildOnly) {
                Write-Host "Build OK. Run: west build -t run --build-dir $BuildPath"
                return
            }

            Write-Host "Running ztest in QEMU (logging to $CoverageLog)..."
            if ($Coverage) {
                Invoke-WestRunWithLog -OutBuildPath $BuildPath -LogPath $CoverageLog
            } else {
                Invoke-West -Args @("build", "-t", "run", "--build-dir", $BuildPath)
            }
        }

        if ($Coverage) {
            Write-Host "Extracting GCOV dump..."
            Import-GcovDumpFromLog -LogPath $CoverageLog
            $gcovExe = Resolve-ZephyrGcovExecutable
            Invoke-CoverageReport -RepoRoot $FrameworkRoot -ObjectDir $BuildPath `
                -Filter $CoverageFilter -MinLine $CoverageMin -GcovExe $gcovExe
        }
        return
    }

    $westExtra = @()
    if ($ConfFile) {
        $westExtra = @("--", "-DCONF_FILE=$ConfFile")
    }

    if ($NoPristine) {
        $westArgs = @("build", "-b", $Board, "-d", $BuildPath, $WestSource) + $westExtra
    } else {
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
