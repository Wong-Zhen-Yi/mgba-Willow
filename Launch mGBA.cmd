@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Use Windows PowerShell without inheriting modules from a developer terminal.
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
if /i "%~1"=="--install-terminal-command" goto install_terminal_command
if /i "%~1"=="--install-startup" goto install_startup
if /i "%~1"=="--remove-startup" goto remove_startup
title mGBA Willow - Game Library + Multi-Agent MCP
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
echo Opening the latest mGBA Willow build with the new game-card library.
echo Choose your ROM folder once; Willow remembers it for future launches.
echo The library scans subfolders and loads ZIP/7z games without manual extraction.
echo Recognized games show their own title splash on the cartridge; artwork is cached after download.
echo Select a game card and click Play, or double-click a card to start.
echo Use File / Return to game library after saving your current game.
echo Multi-agent MCP support remains available during gameplay.
echo Changed emulator sources will be rebuilt automatically.
echo To enable mgba in your terminal, run "Launch mGBA.cmd" --install-terminal-command.
echo The mGBA Willow system tray icon shows when the iPhone link is ready.
echo To add or update mGBA Willow in Windows Startup apps, run "Launch mGBA.cmd" --install-startup.
echo Sign-in launches use the last build; run --remove-startup to undo.
echo To play from your iPhone, click Pair iPhone in the Willow window, then scan the QR code or use Copy Link to send the link to the phone.
echo Tap Auto Play on the phone to let Codex play, or Stop Auto Play to stop the current run.
echo Auto Play models marked Pi run in the Pi agent harness; install it with npm install -g @earendil-works/pi-coding-agent.
echo Tap Settings on the phone for game speed, the Auto Play model, video, button size and keeping the screen awake.
echo Settings on the PC is one page: Willow options first, then the emulator; use the sidebar or Ctrl+F to find a setting.
echo The iPhone link uses Tailscale Funnel HTTPS; Tailscale handles the certificate.
echo Auto video tries NVIDIA NVENC HEVC first, with H.264 and PNG fallbacks.
echo GBA games render on the GPU with OpenGL; Willow uses the software renderer while an AI agent has control.
echo After opening a ROM, multiple agents can connect; button actions run one at a time.
echo Connected agents can update the Objective box with the set_objective MCP tool.
echo After an update, restart the MCP server so agents discover the latest tools.
echo If mGBA is already running, save and close it before rebuilding changed sources.
echo On Ubuntu or other Linux desktops, use "Launch mGBA.sh" instead; run it with --install-desktop for a double-click icon.
echo.
if exist "%~dp0POKEMON_EMERALD_OBJECTIVES.md" (
    echo Pokemon Emerald source of truth: "%~dp0POKEMON_EMERALD_OBJECTIVES.md"
    echo Goal: become Champion, enter the Hall of Fame, and save.
    echo Consult the checklist before gameplay and update it after verified milestones.
) else (
    echo Pokemon Emerald objectives file is missing: "%~dp0POKEMON_EMERALD_OBJECTIVES.md"
)
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-windows.ps1" %*
if not errorlevel 1 exit /b 0
echo.
echo Setup or launch did not finish. Double-click this launcher to retry.
pause
exit /b 1

:install_terminal_command
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-terminal-command.ps1"
exit /b %errorlevel%

:install_startup
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-startup.ps1"
exit /b %errorlevel%

:remove_startup
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-startup.ps1" -Remove
exit /b %errorlevel%
