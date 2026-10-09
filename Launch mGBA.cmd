@echo off
setlocal EnableExtensions DisableDelayedExpansion
title mGBA Willow - Multi-Agent MCP
rem Resolve all launcher paths from this checkout, not the caller's directory.
cd /d "%~dp0"
if errorlevel 1 (
    echo Could not open the mGBA checkout directory.
    pause
    exit /b 1
)
if not exist "%~dp0tools\launch-windows.ps1" (
    echo Missing tools\launch-windows.ps1. Restore the launcher files and retry.
    pause
    exit /b 1
)
echo Opening the latest mGBA Willow build with multi-agent MCP support.
echo Changed emulator sources will be rebuilt automatically.
echo After opening a ROM, multiple agents can connect; button actions run one at a time.
echo.
if exist "%~dp0POKEMON_EMERALD_OBJECTIVES.md" (
    echo Pokemon Emerald source of truth: "%~dp0POKEMON_EMERALD_OBJECTIVES.md"
    echo Goal: become Champion, enter the Hall of Fame, and save.
    echo Consult the checklist before gameplay and update it after verified milestones.
) else (
    echo Pokemon Emerald objectives file is missing: "%~dp0POKEMON_EMERALD_OBJECTIVES.md"
)
echo.
rem Use Windows PowerShell without changing the system execution policy.
rem Do not inherit PowerShell 7 modules from a developer terminal.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-windows.ps1" %*
if not errorlevel 1 exit /b 0
echo.
echo Setup or launch did not finish. Double-click this launcher to retry.
pause
exit /b 1
