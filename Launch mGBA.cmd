@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Launch mGBA
rem Use Windows PowerShell without changing the system execution policy.
rem Do not inherit PowerShell 7 modules from a developer terminal.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-windows.ps1" %*
if not errorlevel 1 exit /b 0
echo.
echo Setup or launch did not finish. Double-click this launcher to retry.
pause
exit /b 1
