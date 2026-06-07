# Resolve framework-only vs framework+app repository layout.
# Dot-source from other scripts: . (Join-Path $PSScriptRoot "project_layout.ps1")
#
# Exported variables after Initialize-ZephyrProjectLayout:
#   ZP_Mode, ZP_AppRoot, ZP_FrameworkRoot, ZP_WorkRoot, ZP_ConfigFile, ZP_TestsDir, ZP_ScriptsRoot

function Read-KeyValueFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    $result = @{}
    if (-not (Test-Path $Path)) {
        return $result
    }

    foreach ($line in Get-Content $Path) {
        if ($line -match '^\s*#') { continue }
        if ($line -match '^\s*$') { continue }
        $eqPos = $line.IndexOf('=')
        if ($eqPos -le 0) { continue }
        $key = $line.Substring(0, $eqPos).Trim()
        $value = $line.Substring($eqPos + 1).Trim()
        if ($key) {
            $result[$key] = $value
        }
    }
    return $result
}

function Test-AppWrapperCMake {
    param([string] $CMakePath)

    if (-not (Test-Path $CMakePath)) {
        return $false
    }
    $content = Get-Content $CMakePath -Raw
    return ($content -match 'add_subdirectory\s*\(\s*framework\b') -or
        ($content -match 'ZEPHYR_[A-Z0-9_]*_TOPLEVEL_BOOTSTRAP')
}

function Get-AppManifest {
    param([string] $AppRoot)

    $manifestPath = Join-Path $AppRoot "zephyr_app.env"
    return Read-KeyValueFile -Path $manifestPath
}

function Get-AppPrjConfName {
    param(
        [string] $AppRoot,
        [hashtable] $Manifest = @{}
    )

    if ($Manifest.ContainsKey("APP_PRJ_CONF") -and $Manifest["APP_PRJ_CONF"]) {
        return $Manifest["APP_PRJ_CONF"]
    }

    $candidates = @(Get-ChildItem -Path $AppRoot -Filter "*_prj.conf" -File -ErrorAction SilentlyContinue)
    if ($candidates.Count -eq 1) {
        return $candidates[0].Name
    }
    if ($candidates.Count -gt 1) {
        $cmakePath = Join-Path $AppRoot "CMakeLists.txt"
        if (Test-Path $cmakePath) {
            if ((Get-Content $cmakePath -Raw) -match 'project\s*\(\s*([A-Za-z0-9_]+)') {
                $projectName = $Matches[1].ToLowerInvariant()
                foreach ($file in $candidates) {
                    if ($file.BaseName.ToLowerInvariant().Contains($projectName)) {
                        return $file.Name
                    }
                }
            }
        }
        return $candidates[0].Name
    }
    return $null
}

function Get-ZephyrQemuConfFile {
    param([hashtable] $Layout)

    if ($env:ZEPHYR_QEMU_CONF) {
        return $env:ZEPHYR_QEMU_CONF
    }

    $manifest = Get-AppManifest -AppRoot $Layout.AppRoot
    if ($manifest.ContainsKey("QEMU_CONF") -and $manifest["QEMU_CONF"]) {
        return $manifest["QEMU_CONF"]
    }

    if ($Layout.Mode -eq "framework") {
        return "prj.conf;prj_qemu.conf"
    }

    $appPrj = Get-AppPrjConfName -AppRoot $Layout.AppRoot -Manifest $manifest
    $fwQemu = Join-Path $Layout.AppRoot "framework/prj_qemu.conf"
    if ($appPrj -and (Test-Path $fwQemu)) {
        return "framework/prj.conf;$appPrj;framework/prj_qemu.conf"
    }
    if ($appPrj) {
        return "framework/prj.conf;$appPrj"
    }
    return $null
}

function Initialize-ZephyrProjectLayout {
    param(
        [string] $ScriptsDir = $PSScriptRoot,
        [ValidateSet("auto", "framework", "app")]
        [string] $Target = "auto"
    )

    if ($env:ZEPHYR_PROJECT_TARGET) {
        $Target = $env:ZEPHYR_PROJECT_TARGET
    }

    $ScriptsRoot = (Resolve-Path $ScriptsDir).Path
    $FrameworkRoot = (Resolve-Path (Join-Path $ScriptsRoot "..")).Path

    $ParentRoot = Split-Path -Parent $FrameworkRoot
    $Mode = "framework"
    $AppRoot = $FrameworkRoot
    $WorkRoot = $FrameworkRoot

    switch ($Target) {
        "app" {
            if (-not (Test-Path (Join-Path $ParentRoot "CMakeLists.txt"))) {
                throw "Target=app but no app wrapper found at $ParentRoot"
            }
            $Mode = "app"
            $AppRoot = $ParentRoot
            $WorkRoot = $ParentRoot
        }
        "framework" {
            $Mode = "framework"
            $AppRoot = $FrameworkRoot
            $WorkRoot = $FrameworkRoot
        }
        default {
            if ((Test-Path (Join-Path $FrameworkRoot "prj.conf")) -and
                (Test-Path (Join-Path $FrameworkRoot "CMakeLists.txt")) -and
                (Test-Path (Join-Path $ParentRoot "CMakeLists.txt")) -and
                (Test-AppWrapperCMake (Join-Path $ParentRoot "CMakeLists.txt"))) {
                $Mode = "app"
                $AppRoot = $ParentRoot
                $WorkRoot = $ParentRoot
            }
        }
    }

    if ($env:ZEPHYR_APP_ROOT) {
        $AppRoot = (Resolve-Path $env:ZEPHYR_APP_ROOT).Path
        if ($Mode -eq "app") {
            $WorkRoot = $AppRoot
        }
    }

    $script:ZP_Mode = $Mode
    $script:ZP_AppRoot = $AppRoot
    $script:ZP_FrameworkRoot = $FrameworkRoot
    $script:ZP_WorkRoot = $WorkRoot
    $script:ZP_ScriptsRoot = $ScriptsRoot
    $script:ZP_ConfigFile = Join-Path $FrameworkRoot "zephyr_config.env"
    $script:ZP_TestsDir = Join-Path $FrameworkRoot "tests"

    $env:ZEPHYR_PROJECT_MODE = $Mode
    $env:ZEPHYR_FRAMEWORK_ROOT = $FrameworkRoot
    $env:ZEPHYR_APP_ROOT = $AppRoot
    $env:ZEPHYR_WORK_ROOT = $WorkRoot

    return @{
        Mode = $Mode
        AppRoot = $AppRoot
        FrameworkRoot = $FrameworkRoot
        WorkRoot = $WorkRoot
        ScriptsRoot = $ScriptsRoot
        ConfigFile = $script:ZP_ConfigFile
        TestsDir = $script:ZP_TestsDir
    }
}

function Get-ZephyrPackageName {
    param([hashtable] $Layout)

    $cmakePath = Join-Path $Layout.AppRoot "CMakeLists.txt"
    if (Test-Path $cmakePath) {
        if ((Get-Content $cmakePath -Raw) -match 'project\s*\(\s*([A-Za-z0-9_]+)') {
            return $Matches[1].ToLowerInvariant()
        }
    }
    return (Split-Path -Leaf $Layout.FrameworkRoot).ToLowerInvariant()
}

function Get-ZephyrAppVersionFile {
    param([hashtable] $Layout)

    foreach ($candidate in @(
            (Join-Path $Layout.AppRoot "APP_VERSION"),
            (Join-Path $Layout.FrameworkRoot "APP_VERSION")
        )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Write-ZephyrProjectBanner {
    param([hashtable] $Layout)

    Write-Host "Project mode: $($Layout.Mode)"
    Write-Host "Framework:    $($Layout.FrameworkRoot)"
    if ($Layout.Mode -eq "app") {
        Write-Host "App root:     $($Layout.AppRoot)"
    }
    Write-Host "Work root:    $($Layout.WorkRoot)"
}
