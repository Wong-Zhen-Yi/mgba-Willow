# Open Willow minimized at Windows sign-in so the iPhone link is always served.
param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'mGBA Willow.lnk'

if ($Remove) {
    if (Test-Path -LiteralPath $shortcutPath) {
        Remove-Item -LiteralPath $shortcutPath
        Write-Host 'Willow no longer opens when you sign in to Windows.'
    } else {
        Write-Host 'Willow was not set to open when you sign in to Windows.'
    }
    return
}

$launcher = Join-Path $PSScriptRoot 'launch-windows.ps1'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Launcher is missing: $launcher"
}
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $powershell
# A hidden PowerShell starts the existing build without the console or the source hash.
$shortcut.Arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$launcher`" -Background"
$shortcut.WorkingDirectory = Split-Path -Parent $PSScriptRoot
$shortcut.WindowStyle = 7
$shortcut.Description = 'Open mGBA Willow minimized and serve Willow Mobile'
$shortcut.Save()
Write-Host 'Willow will open minimized and serve the iPhone link when you sign in to Windows.'
Write-Host 'To undo this, run "Launch mGBA.cmd" --remove-startup.'
