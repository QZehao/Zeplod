<#
.SYNOPSIS
    商业模块本地管理工具 (PowerShell 版本)

.DESCRIPTION
    管理 src/proprietary/ 下的商业模块，支持启用/禁用/状态查看

.PARAMETER Action
    操作命令:
    - status:        查看所有模块状态
    - enable:        启用指定模块
    - disable:       禁用指定模块
    - enable-all:    启用所有模块
    - disable-all:   禁用所有模块
    - check:         检查配置
    - add:           添加新模块到管理列表
    - list:          列出所有可管理的模块

.PARAMETER ModuleName
    模块名称（enable/disable 命令需要）

.EXAMPLE
    .\proprietary_manage.ps1 status
    .\proprietary_manage.ps1 enable event_system_pro
    .\proprietary_manage.ps1 disable-all
    .\proprietary_manage.ps1 check
#>

param(
    [Parameter(Position=0)]
    [ValidateSet('status', 'enable', 'disable', 'enable-all', 'disable-all', 'check', 'list', 'add', 'help')]
    [string]$Action = 'help',

    [Parameter(Position=1)]
    [string]$ModuleName
)

# ============================================================================
# 配置
# ============================================================================
$KconfigFile = "Kconfig"
$ProprietaryDir = "src\proprietary"

# 默认模块列表（可从配置文件读取）
$DefaultModules = @(
    "event_system_pro",
    "mesh_communication",
    "module_manager_pro",
    "ota_manager",
    "security_crypto",
    "cellular_5g_usb",
    "usb_host_cdc_ecm"
)

# ============================================================================
# 工具函数
# ============================================================================

function Write-Success {
    param([string]$Message)
    Write-Host "✓ " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Warning-Custom {
    param([string]$Message)
    Write-Host "⚠ " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "✗ " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Write-Info {
    param([string]$Message)
    Write-Host "ℹ " -ForegroundColor Cyan -NoNewline
    Write-Host $Message
}

function Get-ModuleStatus {
    param([string]$ModuleName)
    
    $ModulePath = Join-Path $ProprietaryDir $ModuleName
    $KconfigPath = Join-Path $ModulePath "Kconfig"
    
    $Status = @{
        Name = $ModuleName
        DirectoryExists = Test-Path $ModulePath
        KconfigExists = Test-Path $KconfigPath
        Enabled = $false
        EnabledInConfig = $false
    }
    
    # 检查 Kconfig 中是否启用
    if (Test-Path $KconfigFile) {
        $Content = Get-Content $KconfigFile
        # 注意：Kconfig 中使用正斜杠
        $EnabledPattern = "source `"src/proprietary/$Name/Kconfig`""
        $DisabledPattern = "# source `"src/proprietary/$Name/Kconfig`""
        
        foreach ($Line in $Content) {
            $TrimmedLine = $Line.Trim()
            if ($TrimmedLine -eq $EnabledPattern) {
                $Status.Enabled = $true
                $Status.EnabledInConfig = $true
                break
            } elseif ($TrimmedLine -eq $DisabledPattern) {
                $Status.Enabled = $false
                $Status.EnabledInConfig = $true
                break
            } elseif ($TrimmedLine -like "*$Name*") {
                # 包含模块名但不完全匹配，可能是注释或其他配置
                $Status.EnabledInConfig = $true
                if ($TrimmedLine -notlike "#*") {
                    $Status.Enabled = $true
                }
                break
            }
        }
    }
    
    return $Status
}

function Show-ModuleStatus {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  商业模块状态" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    
    foreach ($Module in $DefaultModules) {
        $Status = Get-ModuleStatus -ModuleName $Module
        
        # 目录状态
        if ($Status.KconfigExists) {
            $DirStatus = "✓ 已就位"
            $DirColor = "Green"
        } elseif ($Status.DirectoryExists) {
            $DirStatus = "⚠ 目录存在但无 Kconfig"
            $DirColor = "Yellow"
        } else {
            $DirStatus = "✗ 不存在"
            $DirColor = "Red"
        }
        
        # Kconfig 状态
        if ($Status.Enabled) {
            $KconfigStatus = "✓ 已启用"
            $KconfigColor = "Green"
        } elseif ($Status.EnabledInConfig) {
            $KconfigStatus = "- 已禁用"
            $KconfigColor = "Yellow"
        } else {
            $KconfigStatus = "✗ 未配置"
            $KconfigColor = "Red"
        }
        
        Write-Host $Module -ForegroundColor White
        Write-Host "  文件状态: " -NoNewline
        Write-Host $DirStatus -ForegroundColor $DirColor
        Write-Host "  Kconfig:  " -NoNewline
        Write-Host $KconfigStatus -ForegroundColor $KconfigColor
        Write-Host ""
    }
}

function Enable-Module {
    param([string]$Name)
    
    Write-Host ""
    Write-Host "启用模块: $Name" -ForegroundColor Cyan
    
    $ModulePath = Join-Path $ProprietaryDir $Name
    $KconfigPath = Join-Path $ModulePath "Kconfig"
    
    # 检查模块是否存在
    if (-not (Test-Path $KconfigPath)) {
        Write-Error-Custom "模块 $Name 的 Kconfig 不存在"
        Write-Info "请先将模块文件放置到 $ModulePath\"
        return $false
    }
    
    # 取消注释：处理各种注释格式
    # 使用 UTF8 无 BOM 编码读写（Zephyr Kconfig 要求）
    $KconfigContent = [System.IO.File]::ReadAllText((Resolve-Path $KconfigFile), [System.Text.UTF8Encoding]::new($false))
    $TargetLine = "source `"src/proprietary/$Name/Kconfig`""
    
    # 匹配所有可能的注释格式：# source, # # source, #  source 等
    $Pattern = "(#\s*)*" + [regex]::Escape("source `"src/proprietary/$Name/Kconfig`"")
    
    if ($KconfigContent -match $Pattern) {
        $KconfigContent = $KconfigContent -replace $Pattern, $TargetLine
        # 使用 UTF8 无 BOM 编码写回
        [System.IO.File]::WriteAllText((Resolve-Path $KconfigFile), $KconfigContent, [System.Text.UTF8Encoding]::new($false))
        Write-Success "模块 $Name 已启用"
        Write-Info "提示: 请重新运行 CMake 配置以应用更改"
        return $true
    } else {
        Write-Warning-Custom "模块 $Name 在 Kconfig 中未找到"
        return $false
    }
}

function Disable-Module {
    param([string]$Name)
    
    Write-Host ""
    Write-Host "禁用模块: $Name" -ForegroundColor Cyan
    
    # 添加注释：只处理未注释的行
    # 使用 UTF8 无 BOM 编码读写（Zephyr Kconfig 要求）
    $KconfigContent = [System.IO.File]::ReadAllText((Resolve-Path $KconfigFile), [System.Text.UTF8Encoding]::new($false))
    $TargetLine = "source `"src/proprietary/$Name/Kconfig`""
    $CommentedLine = "# source `"src/proprietary/$Name/Kconfig`""
    
    # 精确匹配未注释的行
    $Pattern = "(?m)^" + [regex]::Escape($TargetLine) + "$"
    
    if ($KconfigContent -match $Pattern) {
        $KconfigContent = $KconfigContent -replace $Pattern, $CommentedLine
        # 使用 UTF8 无 BOM 编码写回
        [System.IO.File]::WriteAllText((Resolve-Path $KconfigFile), $KconfigContent, [System.Text.UTF8Encoding]::new($false))
        Write-Success "模块 $Name 已禁用"
        Write-Info "提示: 请重新运行 CMake 配置以应用更改"
        return $true
    } else {
        Write-Warning-Custom "模块 $Name 已禁用或未配置"
        return $false
    }
}

function Enable-AllModules {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  启用所有商业模块" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $SuccessCount = 0
    $SkipCount = 0
    
    foreach ($Module in $DefaultModules) {
        if (Enable-Module -Name $Module) {
            $SuccessCount++
        } else {
            $SkipCount++
        }
    }
    
    Write-Host ""
    Write-Success "启用完成: $SuccessCount 个成功, $SkipCount 个跳过"
    Write-Info "提示: 请重新运行 CMake 配置以应用更改"
}

function Disable-AllModules {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  禁用所有商业模块" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $SuccessCount = 0
    
    foreach ($Module in $DefaultModules) {
        if (Disable-Module -Name $Module) {
            $SuccessCount++
        }
    }
    
    Write-Host ""
    Write-Success "禁用完成: $SuccessCount 个模块"
    Write-Info "提示: 请重新运行 CMake 配置以应用更改"
}

function Check-Configuration {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  检查商业模块配置" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $Errors = 0
    
    if (-not (Test-Path $KconfigFile)) {
        Write-Error-Custom "错误: 找不到 $KconfigFile"
        return
    }
    
    foreach ($Module in $DefaultModules) {
        $Status = Get-ModuleStatus -ModuleName $Module
        
        if (-not $Status.EnabledInConfig) {
            Write-Error-Custom "$Module`: Kconfig 中未配置"
            $Errors++
        } elseif ($Status.Enabled) {
            if (-not $Status.KconfigExists) {
                Write-Error-Custom "$Module`: 已启用但 Kconfig 文件不存在"
                $Errors++
            } else {
                Write-Success "$Module`: 已启用且文件存在"
            }
        } else {
            Write-Warning-Custom "$Module`: 已禁用"
        }
    }
    
    Write-Host ""
    if ($Errors -eq 0) {
        Write-Success "配置检查通过，未发现错误"
    } else {
        Write-Error-Custom "发现 $Errors 个错误"
    }
}

function Show-ModuleList {
    Write-Host ""
    Write-Host "可管理的模块列表:" -ForegroundColor Cyan
    Write-Host ""
    
    foreach ($Module in $DefaultModules) {
        $ModulePath = Join-Path $ProprietaryDir $Module
        $Exists = Test-Path $ModulePath
        
        $Color = if ($Exists) { "Green" } else { "Red" }
        $Status = if ($Exists) { "存在" } else { "不存在" }
        
        Write-Host "  $Module " -NoNewline
        Write-Host "[$Status]" -ForegroundColor $Color
    }
    
    Write-Host ""
}

function Show-Help {
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host "  商业模块本地管理工具" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "用法: .\proprietary_manage.ps1 <命令> [模块名]" -ForegroundColor White
    Write-Host ""
    Write-Host "命令:" -ForegroundColor Yellow
    Write-Host "  status          查看所有模块状态"
    Write-Host "  enable   <模块>  启用指定模块"
    Write-Host "  disable  <模块>  禁用指定模块"
    Write-Host "  enable-all      启用所有商业模块"
    Write-Host "  disable-all     禁用所有商业模块"
    Write-Host "  check           检查 Kconfig 配置"
    Write-Host "  list            列出所有可管理的模块"
    Write-Host "  help            显示此帮助信息"
    Write-Host ""
    Write-Host "可用模块:" -ForegroundColor Yellow
    foreach ($Module in $DefaultModules) {
        Write-Host "  - $Module"
    }
    Write-Host ""
    Write-Host "示例:" -ForegroundColor Yellow
    Write-Host "  .\proprietary_manage.ps1 status"
    Write-Host "  .\proprietary_manage.ps1 enable event_system_pro"
    Write-Host "  .\proprietary_manage.ps1 disable mesh_communication"
    Write-Host "  .\proprietary_manage.ps1 enable-all"
    Write-Host "  .\proprietary_manage.ps1 disable-all"
    Write-Host ""
    Write-Host "提示:" -ForegroundColor Yellow
    Write-Host "  - 修改配置后请重新运行 CMake"
    Write-Host "  - 模块文件应放置在 $ProprietaryDir\<模块名>\ 目录下"
    Write-Host ""
}

# ============================================================================
# 主逻辑
# ============================================================================

switch ($Action) {
    'status' {
        Show-ModuleStatus
    }
    'enable' {
        if (-not $ModuleName) {
            Write-Error-Custom "错误: 请指定模块名"
            Write-Host "用法: .\proprietary_manage.ps1 enable <模块名>" -ForegroundColor Yellow
            Show-ModuleList
        } else {
            Enable-Module -Name $ModuleName
        }
    }
    'disable' {
        if (-not $ModuleName) {
            Write-Error-Custom "错误: 请指定模块名"
            Write-Host "用法: .\proprietary_manage.ps1 disable <模块名>" -ForegroundColor Yellow
            Show-ModuleList
        } else {
            Disable-Module -Name $ModuleName
        }
    }
    'enable-all' {
        Enable-AllModules
    }
    'disable-all' {
        Disable-AllModules
    }
    'check' {
        Check-Configuration
    }
    'list' {
        Show-ModuleList
    }
    'help' {
        Show-Help
    }
    default {
        Show-Help
    }
}
