@echo off
REM =============================================================================
REM Zephyr Map 文件分析脚本 (Windows Batch)
REM =============================================================================
REM 用法：scripts\analyze_map.bat [map 文件路径]
REM =============================================================================

setlocal enabledelayedexpansion

REM 获取脚本目录
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."

REM 帮助信息
if "%~1"=="-h" goto :help
if "%~1"=="--help" goto :help

REM 查找 map 文件
set "MAP_FILE=%~1"
if "%MAP_FILE%"=="" (
    if exist "%PROJECT_ROOT%\release" (
        for /f "delims=" %%i in ('dir /b /o-d "%PROJECT_ROOT%\release\*.map" 2^>nul ^| findstr /n "^" ^| sort /n ^| findstr "^1:"') do (
            for /f "delims=:" %%j in ("%%i") do set "MAP_FILE=%PROJECT_ROOT%\release\%%j"
        )
    )
)

if not exist "%MAP_FILE%" (
    echo 错误：找不到 Map 文件！
    echo 用法：%0 -f ^<path^>
    exit /b 1
)

echo ============================================
echo Zephyr Map 文件分析工具
echo ============================================
echo 分析文件：%MAP_FILE%
echo.

REM =============================================================================
REM 使用 PowerShell 进行解析（更强大）
REM =============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$MapFile = '%MAP_FILE%'; ^
    $Content = Get-Content $MapFile -Raw; ^
    ^
    function Format-Table { param($Data, $Cols); $Data | Format-Table -AutoSize | Out-String }; ^
    ^
    Write-Host '============================================'; ^
    Write-Host '1. BSS 段 (未初始化数据) 分布 - Top 20'; ^
    Write-Host '============================================'; ^
    ^
    $BssMatches = [regex]::Matches($Content, '\\.bss\\.([^\s]+)\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+([^\s]+)'); ^
    $BssData = @(); ^
    foreach ($Match in $BssMatches) { ^
        $Name = $Match.Groups[1].Value; ^
        $Size = [Convert]::ToInt64($Match.Groups[3].Value, 16); ^
        $Module = (Split-Path -Leaf $Match.Groups[4].Value) -replace '\\.obj$$', ''; ^
        if ($Size -gt 0) { ^
            $BssData += [PSCustomObject]@{ ^
                Variable = $Name; ^
                Size = $Size; ^
                SizeKB = [math]::Round($Size / 1KB, 2); ^
                Module = $Module ^
            }; ^
        } ^
    }; ^
    $TopBss = $BssData | Sort-Object Size -Descending | Select-Object -First 20; ^
    $TopBss | Format-Table -Property Variable, @{Label='Size (Bytes)';Expression={$_.Size}}, @{Label='Size (KB)';Expression={$_.SizeKB}}, Module -AutoSize; ^
    $TotalBss = ($BssData | Measure-Object Size -Sum).Sum; ^
    Write-Host ''; ^
    Write-Host \"BSS 段总计：$TotalBss 字节 ($([math]::Round($TotalBss / 1KB, 2)) KB)\"; ^
    Write-Host ''; ^
    ^
    Write-Host '============================================'; ^
    Write-Host '2. NOINIT 段 (不初始化数据) 分布'; ^
    Write-Host '============================================'; ^
    ^
    $NoinitMatches = [regex]::Matches($Content, '\\.noinit\\.([^\s]+)\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)'); ^
    $NoinitData = @(); ^
    foreach ($Match in $NoinitMatches) { ^
        $Name = $Match.Groups[1].Value -replace '^WEST_TOPDIR/', ''; ^
        $Size = [Convert]::ToInt64($Match.Groups[3].Value, 16); ^
        if ($Size -gt 0) { ^
            $NoinitData += [PSCustomObject]@{ ^
                Section = $Name; ^
                Size = $Size; ^
                SizeKB = [math]::Round($Size / 1KB, 2) ^
            }; ^
        } ^
    }; ^
    $NoinitData | Sort-Object Size -Descending | Format-Table -AutoSize; ^
    $TotalNoinit = ($NoinitData | Measure-Object Size -Sum).Sum; ^
    Write-Host ''; ^
    Write-Host \"NOINIT 段总计：$TotalNoinit 字节 ($([math]::Round($TotalNoinit / 1KB, 2)) KB)\"; ^
    Write-Host ''; ^
    ^
    Write-Host '============================================'; ^
    Write-Host '3. 代码段 (.text) 按模块分布'; ^
    Write-Host '============================================'; ^
    ^
    $TextByModule = @{}; ^
    $TextMatches = [regex]::Matches($Content, '\\.text[^\s]*\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+([^\s]+\\.obj)'); ^
    foreach ($Match in $TextMatches) { ^
        $Size = [Convert]::ToInt64($Match.Groups[3].Value, 16); ^
        $Module = (Split-Path -Leaf $Match.Groups[4].Value) -replace '\\.obj$$', ''; ^
        if ($TextByModule.ContainsKey($Module)) { ^
            $TextByModule[$Module] += $Size; ^
        } else { ^
            $TextByModule[$Module] = $Size; ^
        } ^
    }; ^
    $ModuleTable = @(); ^
    foreach ($Key in $TextByModule.Keys) { ^
        $ModuleTable += [PSCustomObject]@{ ^
            Module = $Key; ^
            Size = $TextByModule[$Key]; ^
            SizeKB = [math]::Round($TextByModule[$Key] / 1KB, 2) ^
        }; ^
    }; ^
    $ModuleTable | Sort-Object Size -Descending | Format-Table -AutoSize; ^
    $TotalText = ($ModuleTable | Measure-Object Size -Sum).Sum; ^
    Write-Host ''; ^
    Write-Host \"代码段总计：$TotalText 字节 ($([math]::Round($TotalText / 1KB, 2)) KB)\"; ^
    Write-Host ''; ^
    ^
    Write-Host '============================================'; ^
    Write-Host '4. 只读数据段 (.rodata) 分布 - Top 15'; ^
    Write-Host '============================================'; ^
    ^
    $RodataData = @{}; ^
    $RodataMatches = [regex]::Matches($Content, '\\.rodata[^\s]*\s+(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+([^\s]+)'); ^
    foreach ($Match in $RodataMatches) { ^
        $Size = [Convert]::ToInt64($Match.Groups[3].Value, 16); ^
        $Module = (Split-Path -Leaf $Match.Groups[4].Value) -replace '\\.obj$$', ''; ^
        if ($RodataData.ContainsKey($Module)) { ^
            $RodataData[$Module] += $Size; ^
        } else { ^
            $RodataData[$Module] = $Size; ^
        } ^
    }; ^
    $RodataTable = @(); ^
    foreach ($Key in $RodataData.Keys) { ^
        if ($RodataData[$Key] -gt 0) { ^
            $RodataTable += [PSCustomObject]@{ ^
                Module = $Key; ^
                Size = $RodataData[$Key]; ^
                SizeKB = [math]::Round($RodataData[$Key] / 1KB, 2) ^
            }; ^
        } ^
    }; ^
    $RodataTable | Sort-Object Size -Descending | Select-Object -First 15 | Format-Table -AutoSize; ^
    $TotalRodata = ($RodataTable | Measure-Object Size -Sum).Sum; ^
    Write-Host ''; ^
    Write-Host \"只读数据段总计：$TotalRodata 字节 ($([math]::Round($TotalRodata / 1KB, 2)) KB)\"; ^
    Write-Host ''; ^
    ^
    Write-Host '============================================'; ^
    Write-Host '5. 内存使用总结'; ^
    Write-Host '============================================'; ^
    ^
    $RamTotal = 192 * 1KB; ^
    $RamUsed = $TotalBss + $TotalNoinit; ^
    $RamPercent = [math]::Round(($RamUsed / $RamTotal) * 100, 2); ^
    Write-Host 'RAM 使用情况:'; ^
    Write-Host ('  BSS 段：     {0,12:N0} 字节 ({1,8:N2} KB)' -f $TotalBss, ($TotalBss / 1KB)); ^
    Write-Host ('  NOINIT 段：  {0,12:N0} 字节 ({1,8:N2} KB)' -f $TotalNoinit, ($TotalNoinit / 1KB)); ^
    Write-Host '  ─────────────────────────────'; ^
    Write-Host ('  已使用：     {0,12:N0} 字节 ({1,8:N2} KB) ({2}%)' -f $RamUsed, ($RamUsed / 1KB), $RamPercent); ^
    Write-Host ('  总容量：     {0,12:N0} 字节 ({1,8:N2} KB)' -f $RamTotal, ($RamTotal / 1KB)); ^
    Write-Host ('  剩余可用：   {0,12:N0} 字节 ({1,8:N2} KB)' -f ($RamTotal - $RamUsed), (($RamTotal - $RamUsed) / 1KB)); ^
    Write-Host ''; ^
    ^
    $FlashTotal = 2 * 1MB; ^
    $FlashUsed = $TotalText + $TotalRodata; ^
    $FlashPercent = [math]::Round(($FlashUsed / $FlashTotal) * 100, 2); ^
    Write-Host 'Flash 使用情况:'; ^
    Write-Host ('  代码段：     {0,12:N0} 字节 ({1,8:N2} KB)' -f $TotalText, ($TotalText / 1KB)); ^
    Write-Host ('  只读数据：   {0,12:N0} 字节 ({1,8:N2} KB)' -f $TotalRodata, ($TotalRodata / 1KB)); ^
    Write-Host '  ─────────────────────────────'; ^
    Write-Host ('  已使用：     {0,12:N0} 字节 ({1,8:N2} KB) ({2}%)' -f $FlashUsed, ($FlashUsed / 1KB), $FlashPercent); ^
    Write-Host ('  总容量：     {0,12:N0} 字节 ({1,8:N2} KB)' -f $FlashTotal, ($FlashTotal / 1KB)); ^
    Write-Host ('  剩余可用：   {0,12:N0} 字节 ({1,8:N2} KB)' -f ($FlashTotal - $FlashUsed), (($FlashTotal - $FlashUsed) / 1KB)); ^
    Write-Host ''; ^
    ^
    $OutputFile = '%PROJECT_ROOT%\release\memory_analysis.txt'; ^
    $Report = @(); ^
    $Report += '============================================'; ^
    $Report += 'Zephyr Map 文件分析报告'; ^
    $Report += '============================================'; ^
    $Report += \"分析文件：$MapFile\"; ^
    $Report += \"生成时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')\"; ^
    $Report += ''; ^
    $Report += '=== BSS 段 Top 20 ==='; ^
    $Report += ($TopBss | Format-Table -AutoSize | Out-String); ^
    $Report += ''; ^
    $Report += '=== 代码段按模块 ==='; ^
    $Report += ($ModuleTable | Sort-Object Size -Descending | Format-Table -AutoSize | Out-String); ^
    $Report += ''; ^
    $Report += '=== 内存使用总结 ==='; ^
    $Report += \"RAM 使用：$RamUsed 字节 ($([math]::Round($RamUsed / 1KB, 2)) KB) / $RamTotal 字节 ($([math]::Round($RamTotal / 1KB, 2)) KB) = ${RamPercent}%\"; ^
    $Report += \"Flash 使用：$FlashUsed 字节 ($([math]::Round($FlashUsed / 1KB, 2)) KB) / $FlashTotal 字节 ($([math]::Round($FlashTotal / 1KB, 2)) KB) = ${FlashPercent}%\"; ^
    $Report | Out-File -FilePath $OutputFile -Encoding UTF8; ^
    Write-Host '详细报告已保存到：' -NoNewline; Write-Host $OutputFile -ForegroundColor Green"

goto :end

:help
echo 用法：%0 [选项]
echo.
echo 选项:
echo   -f, --file PATH    Map 文件路径（默认：release/*.map 中最新的文件）
echo   -h, --help         显示帮助
exit /b 0

:end
endlocal
