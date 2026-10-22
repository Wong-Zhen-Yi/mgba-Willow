# Called only after a game loads. A private portable runtime does not alter PATH or system installs.
$ErrorActionPreference = 'Stop'
$existing = Get-Command node.exe -ErrorAction SilentlyContinue
if ($existing) {
    try {
        $version = & $existing.Source --version
        if ($LASTEXITCODE -eq 0 -and $version -match '^v(\d+)\.' -and [int]$Matches[1] -ge 20) {
            Write-Output $existing.Source
            exit 0
        }
    } catch { }
}
$config = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime.json') -Raw | ConvertFrom-Json).node
$root = Join-Path $env:LOCALAPPDATA 'mgba-willow-tools'
$directory = Join-Path $root "node-$($config.version)"
$node = Join-Path $directory "node-$($config.version)-win-x64\node.exe"
$archive = Join-Path $root $config.filename
$lock = $null
try {
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $lock = [IO.File]::Open((Join-Path $root 'node-setup.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    if (-not (Test-Path -LiteralPath $node -PathType Leaf)) {
        Write-Host 'Installing the private Node.js player runtime (about 38 MB)...'
        $valid = (Test-Path -LiteralPath $archive -PathType Leaf) -and
            (Get-Item -LiteralPath $archive).Length -eq $config.bytes -and
            (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -eq $config.sha256
        if (-not $valid) {
            $partial = "$archive.partial"
            if ((Test-Path -LiteralPath $partial) -and (Get-Item -LiteralPath $partial).Length -ge $config.bytes) {
                Remove-Item -LiteralPath $partial -Force
            }
            & "$env:SystemRoot\System32\curl.exe" --fail --location --retry 3 --continue-at - --output $partial $config.url
            if ($LASTEXITCODE -ne 0) { throw 'Node.js download failed; reopen a game to retry/resume.' }
            if ((Get-Item -LiteralPath $partial).Length -ne $config.bytes -or
                (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $config.sha256) {
                Remove-Item -LiteralPath $partial -Force
                throw 'Node.js checksum mismatch. The unverified download was removed.'
            }
            Move-Item -LiteralPath $partial -Destination $archive -Force
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $directory -Force
    }
    $version = & $node --version
    if ($LASTEXITCODE -ne 0 -or $version -ne $config.version) { throw 'Private Node.js runtime verification failed.' }
    Write-Output $node
} catch {
    Write-Host "Player runtime setup: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($lock) { $lock.Dispose() }
}
