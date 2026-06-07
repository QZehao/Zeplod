#!/usr/bin/env pwsh
# =============================================================================
# Proprietary Modules Management Script (PowerShell)
# =============================================================================
# Usage:
#   .\proprietary_manage.ps1 -Enable          # Enable proprietary modules
#   .\proprietary_manage.ps1 -Disable         # Disable proprietary modules
#   .\proprietary_manage.ps1 -Status          # Show status
#   .\proprietary_manage.ps1 -Update          # Update proprietary modules
# =============================================================================

param(
    [switch]$Enable,
    [switch]$Disable,
    [switch]$Status,
    [switch]$Update,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# Color output functions
function Write-Info { Write-Host "[INFO] $args" -ForegroundColor Cyan }
function Write-Success { Write-Host "[OK] $args" -ForegroundColor Green }
function Write-Warning { Write-Host "[WARN] $args" -ForegroundColor Yellow }
function Write-ErrorMsg { Write-Host "[ERROR] $args" -ForegroundColor Red }

# Project paths (proprietary modules live under framework root)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "project_layout.ps1")
$Layout = Initialize-ZephyrProjectLayout -ScriptsDir $ScriptDir
$ProjectRoot = $Layout.FrameworkRoot
$ProprietaryDir = Join-Path $ProjectRoot "src\proprietary"
$ProprietaryCommonCmake = Join-Path $ProprietaryDir "proprietary_modules_common.cmake"
$ConfigFile = Join-Path $ProjectRoot "proprietary_modules.conf"
$LocalConfigFile = Join-Path $ProjectRoot "proprietary_modules.local.conf"

# Module configuration cache
$script:ModuleConfig = @{}

function Show-Help {
    Write-Host @"

Proprietary Modules Management Script

Usage:
    .\proprietary_manage.ps1 -Enable      Enable proprietary modules (init submodule)
    .\proprietary_manage.ps1 -Disable     Disable proprietary modules (remove submodule content)
    .\proprietary_manage.ps1 -Status      Show proprietary modules status
    .\proprietary_manage.ps1 -Update      Update proprietary modules to latest version
    .\proprietary_manage.ps1 -Help        Show this help

Configuration:
    Create 'proprietary_modules.local.conf' to customize module URLs.
    Format: MODULE_NAME=REPOSITORY_URL

    Example:
        MESH_COMMUNICATION_URL=https://gitee.com/your-repo/mesh_communication.git

Description:
    The proprietary modules directory (src/proprietary) is a Git submodule.
    - Enabled: Can configure and use proprietary module features during build
    - Disabled: Project can still compile normally without proprietary modules

"@
}

function Test-GitAvailable {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-ErrorMsg "Git is not installed or not in PATH"
        exit 1
    }
}

function Test-IsGitRepo {
    Test-GitAvailable
    Push-Location $ProjectRoot
    try {
        git rev-parse --git-dir | Out-Null
        return $true
    }
    catch {
        return $false
    }
    finally {
        Pop-Location
    }
}

function Get-SubmoduleStatus {
    Test-GitAvailable
    Push-Location $ProjectRoot
    try {
        $status = git submodule status -- "src/proprietary" 2>$null
        return $status
    }
    finally {
        Pop-Location
    }
}

function Read-ModuleConfig {
    # Read from default config file
    if (Test-Path $ConfigFile) {
        Get-Content $ConfigFile | ForEach-Object {
            $line = $_.Trim()
            if ($line -and !$line.StartsWith('#') -and $line.Contains('=')) {
                $parts = $line.Split('=', 2)
                $key = $parts[0].Trim()
                $value = $parts[1].Trim()
                if ($value) {
                    $script:ModuleConfig[$key] = $value
                }
            }
        }
    }

    # Read from local config file (overrides default)
    if (Test-Path $LocalConfigFile) {
        Get-Content $LocalConfigFile | ForEach-Object {
            $line = $_.Trim()
            if ($line -and !$line.StartsWith('#') -and $line.Contains('=')) {
                $parts = $line.Split('=', 2)
                $key = $parts[0].Trim()
                $value = $parts[1].Trim()
                if ($value) {
                    $script:ModuleConfig[$key] = $value
                }
            }
        }
    }

    # Read from environment variables (highest priority)
    Get-ChildItem Env: | Where-Object { $_.Name -like 'PROPRIETARY_MODULE_*_URL' } | ForEach-Object {
        $key = $_.Name
        $value = $_.Value
        $script:ModuleConfig[$key] = $value
    }
}

function Get-ModuleUrl {
    param([string]$ModuleName)

    $envName = "PROPRIETARY_MODULE_${ModuleName}_URL"
    $configName = "${ModuleName}_URL"

    # Check environment variable first
    $envValue = [Environment]::GetEnvironmentVariable($envName)
    if ($envValue) {
        return $envValue
    }

    # Check config
    if ($script:ModuleConfig.ContainsKey($configName)) {
        return $script:ModuleConfig[$configName]
    }

    return $null
}

function Enable-ProprietaryModules {
    Write-Info "Enabling proprietary modules..."

    if (-not (Test-IsGitRepo)) {
        Write-ErrorMsg "Current directory is not a Git repository"
        exit 1
    }

    # Read configuration
    Read-ModuleConfig

    Push-Location $ProjectRoot
    try {
        # Check if submodule is configured
        $submoduleConfig = git config --file .gitmodules --get "submodule.src/proprietary.url" 2>$null
        if (-not $submoduleConfig) {
            Write-ErrorMsg "Proprietary modules submodule not configured, check .gitmodules file"
            exit 1
        }

        # Initialize main submodule
        Write-Info "Initializing main submodule..."
        $result = git submodule update --init -- "src/proprietary" 2>&1
        $mainExitCode = $LASTEXITCODE

        if ($mainExitCode -ne 0) {
            Write-ErrorMsg "Main submodule initialization failed"
            exit 1
        }

        Write-Success "Main submodule initialized"

        # Check if we need to configure individual modules
        if (Test-Path "$ProprietaryDir/.gitmodules") {
            Write-Info "Configuring individual modules..."

            # Get list of modules from .gitmodules
            $modules = @()
            Push-Location $ProprietaryDir
            try {
                $modules = git config --file .gitmodules --get-regexp path 2>$null | ForEach-Object {
                    $_.Split()[1]
                }
            }
            finally {
                Pop-Location
            }

            $configuredCount = 0
            $skippedCount = 0

            foreach ($moduleName in $modules) {
                $modulePath = Join-Path $ProprietaryDir $moduleName
                $url = Get-ModuleUrl $moduleName

                if ($url) {
                    Write-Host "  Configuring: $moduleName" -NoNewline

                    Push-Location $ProprietaryDir
                    try {
                        # Set the URL for this submodule
                        git config --file .gitmodules "submodule.$moduleName.url" $url 2>$null

                        # Clean up old submodule directory if it exists but is broken
                        if (Test-Path $modulePath) {
                            Remove-Item -Recurse -Force $modulePath 2>$null
                        }

                        # Sync and update
                        git submodule sync -- $moduleName 2>$null

                        # Run update - temporarily disable error action preference
                        $prevErrorAction = $ErrorActionPreference
                        $ErrorActionPreference = "Continue"
                        try {
                            $updateResult = git submodule update --init -- $moduleName 2>&1
                            $updateExitCode = $LASTEXITCODE
                        } finally {
                            $ErrorActionPreference = $prevErrorAction
                        }

                        if ($updateExitCode -eq 0) {
                            Write-Host " [OK]" -ForegroundColor Green
                            $configuredCount++
                        } else {
                            Write-Host " [FAILED]" -ForegroundColor Red
                            Write-Warning "  Could not initialize $moduleName from $url"
                        }
                    }
                    finally {
                        Pop-Location
                    }
                } else {
                    Write-Host "  Skipping: $moduleName (no URL configured)" -ForegroundColor DarkGray
                    $skippedCount++
                }
            }

            Write-Host ""
            Write-Info "Configured: $configuredCount, Skipped: $skippedCount"
        }

        # Verify
        if (Test-Path $ProprietaryCommonCmake) {
            Write-Success "Proprietary modules enabled"
            Write-Info "Directory: $ProprietaryDir"

            # List available modules
            $modules = Get-ChildItem -Path $ProprietaryDir -Directory |
                       Where-Object { Test-Path (Join-Path $_.FullName "Kconfig") }

            if ($modules) {
                Write-Info "Available modules:"
                $modules | ForEach-Object {
                    Write-Host "  - $($_.Name)" -ForegroundColor White
                }
            }
        }
        else {
            Write-Warning "Proprietary modules directory exists but files are incomplete"
        }
    }
    finally {
        Pop-Location
    }
}

function Disable-ProprietaryModules {
    Write-Info "Disabling proprietary modules..."

    if (-not (Test-IsGitRepo)) {
        Write-ErrorMsg "Current directory is not a Git repository"
        exit 1
    }

    Push-Location $ProjectRoot
    try {
        # Check if directory exists
        if (-not (Test-Path $ProprietaryDir)) {
            Write-Warning "Proprietary modules directory does not exist, nothing to disable"
            return
        }

        # Check for uncommitted changes
        if (Test-Path "$ProprietaryDir/.git") {
            Push-Location $ProprietaryDir
            try {
                $status = git status --porcelain 2>$null
                if ($status) {
                    Write-Warning "Proprietary modules directory has uncommitted changes:"
                    Write-Host $status
                    $confirm = Read-Host "Continue with disable? (y/N)"
                    if ($confirm -ne "y" -and $confirm -ne "Y") {
                        Write-Info "Operation cancelled"
                        return
                    }
                }
            }
            finally {
                Pop-Location
            }
        }

        # Remove submodule content (but keep config)
        Write-Info "Removing proprietary modules content..."

        # Use git submodule deinit
        git submodule deinit -f -- "src/proprietary" 2>&1 | Out-Null

        Write-Success "Proprietary modules disabled"
        Write-Info "Project can now compile without proprietary modules"
        Write-Info "To re-enable, run: .\proprietary_manage.ps1 -Enable"
    }
    finally {
        Pop-Location
    }
}

function Show-Status {
    Write-Info "Proprietary modules status:"
    Write-Host ""

    # Check if directory exists
    if (-not (Test-Path $ProprietaryDir)) {
        Write-Warning "Proprietary modules directory does not exist (src/proprietary)"
        Write-Info "Status: Not enabled"
        Write-Info "Run '.\proprietary_manage.ps1 -Enable' to enable"
        return
    }

    # Check if directory is empty (deinit leaves empty dir)
    $dirItems = Get-ChildItem -Path $ProprietaryDir -Force -ErrorAction SilentlyContinue
    if ($dirItems.Count -eq 0) {
        Write-Warning "Proprietary modules directory is empty (src/proprietary)"
        Write-Info "Status: Not enabled"
        Write-Info "Run '.\proprietary_manage.ps1 -Enable' to enable"
        return
    }

    # Check if key files exist
    if (-not (Test-Path $ProprietaryCommonCmake)) {
        Write-Warning "Proprietary modules directory exists but content is incomplete"
        Write-Info "Status: Needs update"
        Write-Info "Run '.\proprietary_manage.ps1 -Update' to fix"
        return
    }

    Write-Success "Framework: Enabled"
    Write-Info "Directory: $ProprietaryDir"
    Write-Host ""

    # Get submodule status
    $submoduleStatus = Get-SubmoduleStatus
    if ($submoduleStatus) {
        $statusChar = $submoduleStatus[0]
        switch ($statusChar) {
            ' ' { Write-Info "Git status: Initialized" }
            '-' { Write-Warning "Git status: Not initialized" }
            '+' { Write-Warning "Git status: Updates available" }
            default { Write-Info "Git status: $submoduleStatus" }
        }
    }

    Write-Host ""
    Write-Info "Available modules:"
    Write-Host ""

    $modules = Get-ChildItem -Path $ProprietaryDir -Directory |
               Where-Object { Test-Path (Join-Path $_.FullName "Kconfig") }

    if ($modules) {
        $modules | ForEach-Object {
            $modulePath = $_.FullName
            $kconfigPath = Join-Path $modulePath "Kconfig"

            # Check if it's a submodule
            $isSubmodule = Test-Path (Join-Path $modulePath ".git")
            $submoduleIndicator = if ($isSubmodule) { " [submodule]" } else { "" }

            Write-Host "  $($_.Name)$submoduleIndicator" -ForegroundColor White

            # Try to read module description
            if (Test-Path $kconfigPath) {
                $content = Get-Content $kconfigPath -Raw
                if ($content -match 'menu\s+"([^"]+)"') {
                    Write-Host "    Description: $($Matches[1])" -ForegroundColor Gray
                }
            }
        }
    }
    else {
        Write-Warning "  No modules available (submodules not initialized)"
        Write-Info "  Configure URLs in proprietary_modules.local.conf and run -Update"
    }

    # Show configuration status
    Write-Host ""
    Write-Info "Configuration:"
    if (Test-Path $LocalConfigFile) {
        Write-Success "  Local config found: proprietary_modules.local.conf"
    } elseif (Test-Path $ConfigFile) {
        Write-Info "  Using default config: proprietary_modules.conf"
        Write-Info "  Create proprietary_modules.local.conf to customize URLs"
    }
}

function Update-ProprietaryModules {
    Write-Info "Updating proprietary modules..."

    if (-not (Test-IsGitRepo)) {
        Write-ErrorMsg "Current directory is not a Git repository"
        exit 1
    }

    # Read configuration
    Read-ModuleConfig

    Push-Location $ProjectRoot
    $hasErrors = $false
    try {
        # Update main submodule
        Write-Info "Updating main proprietary module..."

        $output = git submodule update --init -- "src/proprietary" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Main module updated"
        } else {
            Write-Warning "Main module update had issues, continuing..."
        }

        # Update internal submodules one by one for better error handling
        if (Test-Path "$ProprietaryDir/.gitmodules") {
            Write-Info "Updating internal submodules..."

            Push-Location $ProprietaryDir
            try {
                # Get list of submodule paths
                $submodules = git config --file .gitmodules --get-regexp path 2>$null | ForEach-Object { $_.Split()[1] }

                foreach ($submod in $submodules) {
                    $submodPath = Join-Path $ProprietaryDir $submod
                    $url = Get-ModuleUrl $submod

                    Write-Host "  Checking: $submod" -NoNewline

                    if ($url) {
                        # Update URL if configured
                        git config --file .gitmodules "submodule.$submod.url" $url 2>$null
                        git submodule sync -- $submod 2>$null
                    }

                    Push-Location $submodPath
                    try {
                        $result = git submodule update --init 2>&1
                        if ($LASTEXITCODE -eq 0) {
                            Write-Host " [OK]" -ForegroundColor Green
                        } else {
                            Write-Host " [FAILED]" -ForegroundColor Red
                            if ($url) {
                                Write-Warning "  Error updating $submod from $url"
                            } else {
                                Write-Warning "  No URL configured for $submod"
                            }
                            $hasErrors = $true
                        }
                    }
                    catch {
                        Write-Host " [ERROR]" -ForegroundColor Red
                        $hasErrors = $true
                    }
                    finally {
                        Pop-Location
                    }
                }
            }
            finally {
                Pop-Location
            }
        }

        if ($hasErrors) {
            Write-Warning ""
            Write-Warning "Some submodules failed to update."
            Write-Warning "Configure URLs in proprietary_modules.local.conf"
        } else {
            Write-Success "Proprietary modules updated successfully"
        }
    }
    finally {
        Pop-Location
    }
}

# Main logic
if ($Help -or (-not ($Enable -or $Disable -or $Status -or $Update))) {
    Show-Help
    exit 0
}

if ($Enable) {
    Enable-ProprietaryModules
}
elseif ($Disable) {
    Disable-ProprietaryModules
}
elseif ($Status) {
    Show-Status
}
elseif ($Update) {
    Update-ProprietaryModules
}
