# Windows PowerShell 5.1. Checking never downloads; installation requires explicit consent.
param(
    [ValidateSet('auto', 'cpu', 'cuda')][string]$Device = 'auto',
    [switch]$OfferInstall,
    [switch]$RequireInstalled,
    [switch]$Json
)
$ErrorActionPreference = 'Stop'
try {
    if ($Device -eq 'auto') {
        $Device = 'cpu'
        $nvidia = Get-Command nvidia-smi -ErrorAction SilentlyContinue
        if ($nvidia) {
            try {
                $null = & $nvidia.Source --query-gpu=name --format=csv,noheader 2>$null
                if ($LASTEXITCODE -eq 0) { $Device = 'cuda' }
            } catch { $Device = 'cpu' }
        }
    }
    $powershell = Join-Path $PSHOME 'powershell.exe'
    $serve = Join-Path $PSScriptRoot 'serve.ps1'
    function Read-Installation {
        $output = & $powershell -NoProfile -ExecutionPolicy Bypass -File $serve -Device $Device -CheckInstalled
        if ($LASTEXITCODE -ne 0) { throw 'Could not check the local model installation.' }
        return ($output -join "`n") | ConvertFrom-Json
    }
    $status = Read-Installation
    if ($Json) { $status | ConvertTo-Json; exit 0 }
    Write-Host ''
    Write-Host "Local model: $($status.model)"
    if ($status.installed) {
        Write-Host "Local Kev Q4_K_M is installed (SHA-256 verified); $Device runtime is ready."
        exit 0
    }
    if (-not $status.modelInstalled) { Write-Host "Local model is $($status.modelStatus)." }
    if (-not $status.runtimeInstalled) { Write-Host "Local $Device runtime is missing or incomplete: $($status.runtime)" }
    if ($OfferInstall) {
        Write-Host 'First setup downloads the 2.54 GB model plus runtime files. Verified files are reused.'
        Write-Host 'This is optional for normal emulator use and does not start AI gameplay.'
        $answer = Read-Host 'Download/install or repair the local model now? [y/N]'
        if ($answer -match '^(y|yes)$') {
            & $powershell -NoProfile -ExecutionPolicy Bypass -File $serve -Device $Device -DownloadOnly
            if ($LASTEXITCODE -ne 0) { throw 'Local model setup failed. Click Enable local model in mGBA to retry/resume.' }
            $status = Read-Installation
            if (-not $status.installed) { throw 'Local model setup did not pass verification.' }
            Write-Host 'Local model and runtime are installed and ready.'
            exit 0
        }
    }
    Write-Host 'Local AI is not installed. Load a game in mGBA and click Enable local model to set it up.'
    if ($RequireInstalled) { exit 1 }
} catch {
    Write-Host "Local model check: $($_.Exception.Message)" -ForegroundColor Yellow
    exit 1
}
