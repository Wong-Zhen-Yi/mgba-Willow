@echo off
setlocal EnableExtensions DisableDelayedExpansion
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
echo Select a game card and click Play, or double-click a card to start.
echo Use File / Return to game library after saving your current game.
echo Multi-agent MCP support remains available during gameplay.
echo Local AI is opt-in: load a game, then click Enable local model in mGBA.
echo Choose a preset goal: Progress the story, Level up Pokemon, Explore nearby, or Finish the current battle.
echo Click Start with selected goal, or Enable model only to keep manual control.
echo Missing Kev/runtime files are installed only after you confirm in that dialog.
echo Opening the app or loading a game alone never starts Kev or downloads models.
echo GGUF models are stored in "%LOCALAPPDATA%\mgba-willow\models".
echo Local AI currently requires decoded English Emerald; other GBA games need a state adapter.
echo Local AI supports 10,000-turn / 12-hour runs, remembers routes, and waits through animations.
echo Navigation scores account for the number of choices; confirmation safety checks remain enabled.
echo Optional GPT-6 Luna help: set LUNA_API_KEY and LUNA_MODEL before launch, then tick Ask GPT-6 Luna.
echo Luna help is paid and shares decoded game state; it never bypasses unknown prompts.
echo Changed emulator sources will be rebuilt automatically.
echo After opening a ROM, multiple agents can connect; button actions run one at a time.
echo Connected agents can update the Objective box with the set_objective MCP tool.
echo After an update, restart the MCP server so agents discover the latest tools.
echo If mGBA is already running, save and close it before rebuilding changed sources.
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
