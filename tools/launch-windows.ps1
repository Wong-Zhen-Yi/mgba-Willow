# Windows PowerShell 5.1 compatible; invoked by Launch mGBA.cmd.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArguments)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build-auto-windows'
$logPath = Join-Path $buildDir 'launcher.log'
$toolsDir = Join-Path $env:LOCALAPPDATA 'mgba-willow-tools'
$msysRoot = Join-Path $toolsDir 'msys64'
$lockStream = $null
$transcribing = $false
$buildReady = $null
# Include source changes, not just the build script, in the cached build identity.
$buildInputs = @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src'), (Join-Path $repoRoot 'include'), (Join-Path $repoRoot 'res') -Recurse -File
    Get-Item -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'), (Join-Path $repoRoot 'version.cmake'), (Join-Path $PSScriptRoot 'build-windows.sh')
)
$buildIdentity = ($buildInputs | Sort-Object FullName | ForEach-Object {
    $_.FullName.Substring($repoRoot.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}) -join "`n"
$recipeHasher = [Security.Cryptography.SHA256]::Create()
try {
    $buildRecipe = [BitConverter]::ToString($recipeHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($buildIdentity))).Replace('-', '')
} finally { $recipeHasher.Dispose() }

function Invoke-Native {
    param([string]$Program, [string[]]$NativeArguments)
    & $Program @NativeArguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed (exit code $LASTEXITCODE). See the details above."
    }
}

function Invoke-Msys {
    param([string]$Command)
    Invoke-Native (Join-Path $msysRoot 'usr\bin\bash.exe') @('--login', '-c', $Command)
}

function Invoke-PackageInstall {
    param([string]$Command)
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-Msys $Command
            return
        } catch {
            if ($attempt -eq 3) { throw }
            Write-Host 'A package download failed. Retrying with the cached downloads...'
            Start-Sleep -Seconds 3
        }
    }
}

function Set-MsysEnvironment {
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = 'yes'
    $env:MSYS2_PATH_TYPE = 'inherit'
    $env:PATH = "$(Join-Path $msysRoot 'ucrt64\bin');$env:PATH"
    $env:QT_PLUGIN_PATH = Join-Path $msysRoot 'ucrt64\share\qt6\plugins'
}

function Install-BuildTools {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'Automatic building requires 64-bit Windows 10 or newer.'
    }
    New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
    # Use a Windows short path where available for MSYS2 tools.
    $fileSystem = New-Object -ComObject Scripting.FileSystemObject
    $script:toolsDir = $fileSystem.GetFolder($toolsDir).ShortPath
    $script:msysRoot = Join-Path $toolsDir 'msys64'

    if (-not (Test-Path -LiteralPath (Join-Path $toolsDir 'base-ready')) -or
        -not (Test-Path -LiteralPath (Join-Path $msysRoot 'usr\bin\bash.exe'))) {
        Write-Host '[1/4] Downloading and unpacking the build tools...'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        # Official pinned base archive, checked before extraction.
        $archiveUrl = 'https://github.com/msys2/msys2-installer/releases/download/2026-09-27/msys2-base-x86_64-20260927.tar.xz'
        $archiveHash = 'ea2f31a0b6ade63914ce441ffb022f0f6aa96982bfefa2326460a26d5fb01322'
        $archivePath = Join-Path $toolsDir 'msys2-base.tar.xz'
        if (-not (Test-Path -LiteralPath $archivePath) -or
            (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $archiveHash) {
            if (Test-Path -LiteralPath "$env:SystemRoot\System32\curl.exe") {
                Invoke-Native "$env:SystemRoot\System32\curl.exe" @(
                    '--fail', '--location', '--retry', '3', '--progress-bar',
                    '--output', $archivePath, $archiveUrl)
            } else {
                Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archivePath
            }
        }
        if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $archiveHash) {
            throw 'The build-tools download is incomplete or corrupt. Please retry.'
        }
        Invoke-Native "$env:SystemRoot\System32\tar.exe" @('-xf', $archivePath, '-C', $toolsDir)
        Set-MsysEnvironment
        Invoke-Msys 'true'
        Set-Content -LiteralPath (Join-Path $toolsDir 'base-ready') -Value '2026-09-27'
    }
    Set-MsysEnvironment

    # Prefer the official origin over the geo redirector. Keep the published
    # mirrors below it as fallbacks, within this private installation only.
    foreach ($mirror in @('mingw', 'msys')) {
        $mirrorPath = Join-Path $msysRoot "etc\pacman.d\mirrorlist.$mirror"
        $origin = if ($mirror -eq 'mingw') {
            'Server = https://repo.msys2.org/mingw/$repo/'
        } else {
            'Server = https://repo.msys2.org/msys/$arch/'
        }
        $mirrorText = Get-Content -LiteralPath $mirrorPath -Raw
        if (-not $mirrorText.StartsWith($origin)) {
            Set-Content -LiteralPath $mirrorPath -Encoding ascii -Value ($origin + "`n" + $mirrorText)
        }
    }

    $packagesReady = Join-Path $toolsDir 'packages-ready-v1'
    $missingTool = @('cmake.exe', 'gcc.exe', 'g++.exe', 'ninja.exe',
        'Qt6Core.dll', 'Qt6Multimedia.dll') | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $msysRoot "ucrt64\bin\$_"))
    }
    if (-not (Test-Path -LiteralPath $packagesReady) -or
        $missingTool.Count -gt 0) {
        Write-Host '[2/4] Installing the compiler and app dependencies...'
        Write-Host 'This first-time download can take a while. No commands need to be typed.'
        # Start a fresh shell between the core update and full update.
        Invoke-PackageInstall 'pacman --noconfirm -Syuu'
        Invoke-PackageInstall 'pacman --noconfirm -Syuu'
        Invoke-PackageInstall ('pacman --noconfirm -S --needed git ' +
            'mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake ' +
            'mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf ' +
            'mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-multimedia ' +
            'mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-libepoxy ' +
            'mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-libpng ' +
            'mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-lua ' +
            'mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-libelf')
        Set-Content -LiteralPath $packagesReady -Value 'ready'
    }
}

try {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    try {
        $lockStream = [IO.File]::Open((Join-Path $buildDir 'launcher.lock'),
            [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch {
        throw 'Another launcher is already setting up this checkout. Please wait for that window to finish.'
    }
    Start-Transcript -LiteralPath $logPath -Force | Out-Null
    $transcribing = $true
    # Always use the managed build from this checkout; unrelated binaries may
    # lack the AI bridge even when they are named mGBA.exe.
    $appPath = $null
    if (-not $appPath) {
        $appPath = Join-Path $buildDir 'mGBA.exe'
        $buildReady = Join-Path $buildDir 'build-ready'
        if (-not (Test-Path -LiteralPath $appPath) -or
            -not (Test-Path -LiteralPath $buildReady) -or
            (Get-Content -LiteralPath $buildReady -ErrorAction SilentlyContinue) -ne $buildRecipe -or
            -not (Test-Path -LiteralPath (Join-Path $msysRoot 'ucrt64\bin\Qt6Core.dll'))) {
            Write-Host 'Setting up mGBA automatically. Keep this window open until the app starts.'
            Write-Host 'The first run needs internet access and several GB of free disk space.'
            Install-BuildTools
            Write-Host '[3/4] Building mGBA from this checkout...'
            $env:MGBA_SOURCE_ROOT = $repoRoot
            $env:MGBA_BUILD_ROOT = $buildDir
            $env:MGBA_BUILD_SCRIPT = Join-Path $PSScriptRoot 'build-windows.sh'
            # Pass a script filename directly to avoid legacy PowerShell's
            # embedded-quote handling when sending a command to bash -c.
            Invoke-Native (Join-Path $msysRoot 'usr\bin\bash.exe') @(
                '--login', ($env:MGBA_BUILD_SCRIPT -replace '\\', '/'))
            if (-not (Test-Path -LiteralPath $appPath)) {
                throw 'The build finished without producing the Windows app.'
            }
        }
        Set-MsysEnvironment
    }
    Write-Host '[4/4] Opening mGBA...'
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $appPath
    $startInfo.WorkingDirectory = Split-Path -Parent $appPath
    $startInfo.UseShellExecute = $false
    # Escape using Windows command-line rules, including trailing backslashes.
    $quotedArguments = foreach ($argument in $AppArguments) {
        '"' + ([regex]::Replace(([regex]::Replace($argument, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
    }
    $startInfo.Arguments = $quotedArguments -join ' '
    $appProcess = [Diagnostics.Process]::Start($startInfo)
    if ($appProcess.WaitForExit(2000)) {
        if ($appProcess.ExitCode -ne 0 -or $AppArguments.Count -eq 0) {
            throw "mGBA stopped during startup (exit code $($appProcess.ExitCode))."
        }
    }
    if ($buildReady) { Set-Content -LiteralPath $buildReady -Value $buildRecipe }
} catch {
    Write-Host ''
    Write-Host "mGBA could not start: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Details are saved in: $logPath"
    exit 1
} finally {
    if ($transcribing) { Stop-Transcript | Out-Null }
    if ($lockStream) { $lockStream.Dispose() }
}
