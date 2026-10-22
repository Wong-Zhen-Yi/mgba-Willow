# Windows PowerShell 5.1. Pinned, checksum-verified GGUF + llama.cpp; no Python required.
param(
    [ValidateSet('cpu', 'cuda')][string]$Device = 'cuda',
    [ValidateRange(1024, 32768)][int]$ContextSize = 8192,
    [ValidateRange(0, 99)][int]$GpuLayers = 20,
    [ValidateRange(1024, 65535)][int]$Port = 8008,
    [switch]$DownloadOnly,
    [switch]$ShowPaths,
    [switch]$CheckInstalled
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime.json') -Raw | ConvertFrom-Json
$modelDir = Join-Path $env:LOCALAPPDATA 'mgba-willow\models'
$modelPath = Join-Path $modelDir $config.model.filename
$toolsDir = Join-Path $env:LOCALAPPDATA 'mgba-willow-tools'
$runtimeDir = Join-Path $toolsDir "llama-$($config.runtime.version)-$Device"
$downloadsDir = Join-Path $toolsDir 'downloads'
$layers = if ($Device -eq 'cpu') { 0 } else { $GpuLayers }
$serverArguments = @('-m', $modelPath, '--alias', 'kev-latest', '--host', '127.0.0.1',
    '--port', "$Port", '-c', "$ContextSize", '-np', '1', '-ub', '4096', '-b', '4096',
    '-ngl', "$layers", '--cache-ram', '0')
# Allows inspecting paths/settings without downloading, installing or starting anything.
if ($ShowPaths) {
    [ordered]@{ model = $modelPath; runtime = $runtimeDir; downloads = $downloadsDir;
        device = $Device; arguments = $serverArguments } | ConvertTo-Json -Depth 4
    exit 0
}

function Test-VerifiedFile {
    param($Asset, [string]$File)
    if (-not (Test-Path -LiteralPath $File)) { return $false }
    if ((Get-Item -LiteralPath $File).Length -ne $Asset.bytes) { return $false }
    return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash -eq $Asset.sha256
}

function Get-InstalledServer {
    $ready = Join-Path $runtimeDir 'runtime-ready'
    if (-not (Test-Path -LiteralPath $ready -PathType Leaf) -or
        (Get-Content -LiteralPath $ready -Raw).Trim() -ne $config.runtime.$Device.sha256) { return $null }
    $servers = @(Get-ChildItem -LiteralPath $runtimeDir -Recurse -Filter 'llama-server.exe' -File)
    if ($servers.Count -ne 1) { return $null }
    $required = @('llama-server-impl.dll', 'llama.dll', 'ggml.dll', 'ggml-base.dll')
    if ($Device -eq 'cuda') {
        $required += @('ggml-cuda.dll', 'cudart64_13.dll', 'cublas64_13.dll', 'cublasLt64_13.dll')
    }
    foreach ($file in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $servers[0].DirectoryName $file) -PathType Leaf)) { return $null }
    }
    return $servers[0]
}

# Read-only installation check used only after the user enables the local model.
if ($CheckInstalled) {
    $modelInstalled = Test-VerifiedFile $config.model $modelPath
    $server = Get-InstalledServer
    [ordered]@{ installed = $modelInstalled -and $null -ne $server;
        modelInstalled = $modelInstalled; runtimeInstalled = $null -ne $server;
        modelStatus = $(if ($modelInstalled) { 'verified' } elseif (Test-Path -LiteralPath $modelPath) { 'invalid' } else { 'missing' });
        model = $modelPath; runtime = $runtimeDir; device = $Device } | ConvertTo-Json
    exit 0
}

function Receive-VerifiedFile {
    param($Asset, [string]$Destination)
    if (Test-VerifiedFile $Asset $Destination) {
        Write-Host "Using verified file: $Destination"
        return
    }
    $partial = "$Destination.partial"
    if (-not (Test-VerifiedFile $Asset $partial)) {
        if ((Test-Path -LiteralPath $partial) -and (Get-Item -LiteralPath $partial).Length -ge $Asset.bytes) {
            Remove-Item -LiteralPath $partial -Force
        }
        Write-Host "Downloading $($Asset.filename)..."
        Write-Host "Destination: $Destination"
        & "$env:SystemRoot\System32\curl.exe" --fail --location --retry 3 --retry-delay 2 `
            --continue-at - --output $partial $Asset.url
        if ($LASTEXITCODE -ne 0) { throw 'Download interrupted. Click Enable local model in mGBA again to resume.' }
        if (-not (Test-VerifiedFile $Asset $partial)) {
            Remove-Item -LiteralPath $partial -Force
            throw "Checksum mismatch for $($Asset.filename). The unverified download was removed; retry."
        }
    }
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}

$lock = $null
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'The bundled llama.cpp runtime requires 64-bit Windows.' }
    Write-Host 'Kev-4B Artemis Q4_K_M (GGUF) - local text decision server'
    Write-Host "Model: $modelPath"
    Write-Host "Runtime: $runtimeDir"
    Write-Host "Endpoint: http://127.0.0.1:$Port/v1/systemone"
    Write-Host "Settings: $Device, $layers GPU layers, $ContextSize context, one request slot."
    Write-Host 'First setup downloads the 2.54 GB model and the llama.cpp runtime. No Python/base-model download.'
    Write-Host 'Default CUDA settings use partial CPU offload for 8 GB GPUs; actual memory usage needs verification.'
    Write-Host 'If GPU memory runs out, retry with -GpuLayers 12 or -Device cpu.'
    New-Item -ItemType Directory -Path $modelDir, $runtimeDir, $downloadsDir -Force | Out-Null
    try {
        $lock = [IO.File]::Open((Join-Path $toolsDir 'kev-setup.lock'),
            [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch { throw 'Another Kev setup is downloading/installing. Wait for it to finish and retry.' }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Receive-VerifiedFile $config.model $modelPath
    $package = $config.runtime.$Device
    $archive = Join-Path $downloadsDir $package.filename
    $ready = Join-Path $runtimeDir 'runtime-ready'
    $server = Get-InstalledServer
    if (-not $server) {
        Receive-VerifiedFile $package $archive
        Expand-Archive -LiteralPath $archive -DestinationPath $runtimeDir -Force
        $servers = @(Get-ChildItem -LiteralPath $runtimeDir -Recurse -Filter 'llama-server.exe' -File)
        if ($servers.Count -ne 1) { throw 'The runtime archive did not contain exactly one llama-server.exe.' }
        if ($Device -eq 'cuda') {
            $dependencies = $config.runtime.cudaDependencies
            $dependencyArchive = Join-Path $downloadsDir $dependencies.filename
            Receive-VerifiedFile $dependencies $dependencyArchive
            Expand-Archive -LiteralPath $dependencyArchive -DestinationPath $servers[0].DirectoryName -Force
        }
        Set-Content -LiteralPath $ready -Value $package.sha256 -Encoding ascii
        $server = Get-InstalledServer
        if (-not $server) { throw 'The runtime is incomplete after extraction. Retry setup to repair it.' }
    }
    $lock.Dispose()
    $lock = $null
    if ($DownloadOnly) {
        Write-Host 'Model and runtime installed and verified. The enabled local-model helper will start inference.'
        exit 0
    }
    # Preserve the existing client/server key convention without exposing a key on the command line.
    $env:LLAMA_API_KEY = $env:KEV_API_KEY
    Write-Host 'Starting llama-server. Leave this console open; Ctrl+C stops inference.'
    & $server.FullName @serverArguments
    if ($LASTEXITCODE -ne 0) { throw "llama-server stopped (exit $LASTEXITCODE). See the console above for details." }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Expected model location: $modelPath"
    Write-Host 'See tools\local-model\README.md. No emulator saves or MCP registration were changed.'
    exit 1
} finally {
    if ($lock) { $lock.Dispose() }
}
