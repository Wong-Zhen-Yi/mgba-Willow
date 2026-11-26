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
$compiler = Join-Path $env:SystemRoot 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'The Windows .NET Framework compiler is required to install the mGBA Willow startup app.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$icon = Join-Path $repoRoot 'res\mgba.ico'
$source = Join-Path $PSScriptRoot 'WillowStartup.cs'
$toolsDir = Join-Path $env:LOCALAPPDATA 'mgba-willow-tools'
$appPath = Join-Path $toolsDir 'mGBA Willow.exe'
$stagingDir = Join-Path $toolsDir ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
$stagedApp = Join-Path $stagingDir 'mGBA Willow.exe'
try {
    & $compiler /nologo /target:winexe /optimize+ /reference:System.Windows.Forms.dll "/win32icon:$icon" "/out:$stagedApp" $source
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not build the mGBA Willow startup app.'
    }
    if (Test-Path -LiteralPath $appPath) {
        [IO.File]::Replace($stagedApp, $appPath, [NullString]::Value)
    } else {
        [IO.File]::Move($stagedApp, $appPath)
    }
} finally {
    if (Test-Path -LiteralPath $stagedApp) { Remove-Item -LiteralPath $stagedApp }
    Remove-Item -LiteralPath $stagingDir
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $appPath
$shortcut.Arguments = '"' + $launcher + '"'
$shortcut.WorkingDirectory = $repoRoot
$shortcut.IconLocation = "$appPath,0"
$shortcut.WindowStyle = 1
$shortcut.Description = 'Open mGBA Willow minimized and serve Willow Mobile'
$shortcut.Save()
Write-Host 'mGBA Willow will open minimized at Windows sign-in. Check its tray icon for server status.'
Write-Host 'To undo this, run "Launch mGBA.cmd" --remove-startup.'
