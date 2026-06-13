#!/usr/bin/env pwsh
# =============================================================================
# Proprietary module manager (stub)
# =============================================================================
# Usage: scripts/proprietary_manage.ps1 <status|enable|disable|enable-all|disable-all|check>
# =============================================================================

param(
    [Parameter(Position = 0)]
    [string]$Command = "status",

    [Parameter(Position = 1)]
    [string]$Module = ""
)

$KnownModules = @(
    "mesh_communication",
    "module_manager_pro",
    "ota_manager",
    "security_crypto",
    "cellular_5g_usb",
    "usb_host_cdc_ecm"
)

function Show-Usage {
    Write-Host "Usage: proprietary_manage.ps1 <command> [module]"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  status              Show proprietary module availability"
    Write-Host "  enable  <module>    Enable a proprietary module (requires files in src/proprietary/)"
    Write-Host "  disable <module>    Disable a proprietary module"
    Write-Host "  enable-all          Enable all known proprietary modules"
    Write-Host "  disable-all         Disable all proprietary modules"
    Write-Host "  check               Verify Kconfig / CMake consistency"
    Write-Host "  help                Show this help"
    Write-Host ""
    Write-Host "Proprietary modules are not included in the open-source repository."
    Write-Host "Contact china_qzh@163.com for commercial licensing."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FrameworkRoot = Resolve-Path (Join-Path $ScriptDir "..") | Select-Object -ExpandProperty Path
$ProprietaryDir = Join-Path $FrameworkRoot "src" "proprietary"

function Test-ProprietaryAvailable {
    return Test-Path $ProprietaryDir -PathType Container
}

function Get-AvailableModules {
    if (-not (Test-ProprietaryAvailable)) {
        return @()
    }
    return Get-ChildItem -Path $ProprietaryDir -Directory | Select-Object -ExpandProperty Name
}

switch ($Command.ToLowerInvariant()) {
    "help" { Show-Usage; exit 0 }
    "status" {
        Write-Host "Proprietary module status"
        Write-Host "========================="
        $available = Get-AvailableModules
        if ($available.Count -eq 0) {
            Write-Host "No proprietary modules present in src/proprietary/."
            Write-Host "Contact china_qzh@163.com for commercial licensing."
        } else {
            foreach ($m in $KnownModules) {
                $state = if ($available -contains $m) { "AVAILABLE" } else { "NOT PRESENT" }
                Write-Host ("  {0,-25} {1}" -f $m, $state)
            }
        }
        exit 0
    }
    "check" {
        if (-not (Test-ProprietaryAvailable)) {
            Write-Host "check: src/proprietary/ does not exist; nothing to verify."
            exit 0
        }
        $available = Get-AvailableModules
        Write-Host "check: found $($available.Count) proprietary module(s): $($available -join ', ')"
        exit 0
    }
    "enable" {
        if ([string]::IsNullOrWhiteSpace($Module)) {
            Write-Error "enable: module name required"; exit 1
        }
        if (-not (Test-ProprietaryAvailable) -or -not ((Get-AvailableModules) -contains $Module)) {
            Write-Error "enable: '$Module' is not available. Proprietary modules require commercial licensing."
            exit 1
        }
        Write-Host "enable: '$Module' is present but CMake/Kconfig integration must be configured manually."
        exit 0
    }
    "disable" {
        if ([string]::IsNullOrWhiteSpace($Module)) {
            Write-Error "disable: module name required"; exit 1
        }
        Write-Host "disable: '$Module' disabled (no proprietary modules are enabled by default in this repository)."
        exit 0
    }
    "enable-all" {
        if (-not (Test-ProprietaryAvailable)) {
            Write-Host "enable-all: no proprietary modules present."
            exit 0
        }
        $available = Get-AvailableModules
        Write-Host "enable-all: found $($available.Count) module(s): $($available -join ', ')"
        Write-Host "enable-all: CMake/Kconfig integration must be configured manually."
        exit 0
    }
    "disable-all" {
        Write-Host "disable-all: all proprietary modules are now disabled."
        exit 0
    }
    default {
        Write-Error "Unknown command: $Command"; Show-Usage; exit 1
    }
}
