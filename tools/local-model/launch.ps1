# Windows PowerShell 5.1. Started ONLY by mGBA's Enable local model button.
param(
    [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$EmulatorPid,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f-]{36}$')][string]$SessionId,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f-]{36}$')][string]$GameId,
    [ValidateSet('auto', 'cpu', 'cuda')][string]$Device = 'auto',
    [string]$GoalId = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'session.ps1')
. (Join-Path $PSScriptRoot 'goals.ps1')
$ownedServer = $null
$lock = $null

function Read-GameInput($Emulator, $Session, [string]$Prompt) {
    if ([Console]::IsInputRedirected) { return Read-Host $prompt }
    Write-Host "$prompt`: " -NoNewline
    $text = ''
    $check = [Diagnostics.Stopwatch]::StartNew()
    while (Test-EmulatorAlive $Emulator) {
        if ($check.ElapsedMilliseconds -ge 1000) {
            if (-not (Test-CurrentGame $Emulator $Session)) { Write-Host ''; return '' }
            $check.Restart()
        }
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq 'Enter') { Write-Host ''; return $text }
            if ($key.Key -eq 'Backspace') {
                if ($text.Length) { $text = $text.Substring(0, $text.Length - 1); Write-Host "`b `b" -NoNewline }
            } elseif (-not [char]::IsControl($key.KeyChar) -and $text.Length -lt 512) {
                $text += $key.KeyChar
                Write-Host $key.KeyChar -NoNewline
            }
        } else { Start-Sleep -Milliseconds 100 }
    }
    Write-Host ''
    return ''
}

function Read-GameGoal($Emulator, $Session, $Presets) {
    while (Test-CurrentGame $Emulator $Session) {
        Write-Host 'Choose a gameplay goal (no typing of instructions needed):'
        for ($i = 0; $i -lt $Presets.Count; ++$i) { Write-Host ("  {0}. {1}" -f ($i + 1), $Presets[$i].label) }
        Write-Host '  C. Custom goal (optional)'
        Write-Host '  0. Keep manual control'
        $choice = Read-GameInput $Emulator $Session 'Preset number (Enter keeps manual control)'
        if ([string]::IsNullOrWhiteSpace($choice) -or $choice.Trim() -eq '0') { return '' }
        if ($choice.Trim() -ieq 'c') { return Read-GameInput $Emulator $Session 'Custom goal (optional)' }
        $number = 0
        if ([int]::TryParse($choice.Trim(), [ref]$number) -and $number -ge 1 -and $number -le $Presets.Count) {
            return $Presets[$number - 1].goal
        }
        Write-Host 'Choose a listed number, C for a custom goal, or Enter for manual control.'
    }
    return ''
}

try {
    $emulator = Get-Process -Id $EmulatorPid -ErrorAction Stop
    $session = Get-LoadedGame $EmulatorPid
    if (-not $session -or $session.session_id -ne $SessionId -or $session.game_id -ne $GameId) {
        throw 'The selected game is no longer open. Load the game and click Enable local model again.'
    }
    $presets = @(Get-GameplayPresets)
    $initialGoal = ''
    if (-not [string]::IsNullOrWhiteSpace($GoalId)) { $initialGoal = Resolve-GameplayPreset $presets $GoalId }
    $powershell = Join-Path $PSHOME 'powershell.exe'
    Write-Host "Local model enabled by you for: $($session.title)"
    $tools = Join-Path $env:LOCALAPPDATA 'mgba-willow-tools'
    New-Item -ItemType Directory -Path $tools -Force | Out-Null
    $lock = [IO.File]::Open((Join-Path $tools "local-player-$EmulatorPid.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
    $checker = Join-Path $PSScriptRoot 'check-install.ps1'
    $output = & $powershell -NoProfile -ExecutionPolicy Bypass -File $checker -Device $Device -Json
    if ($LASTEXITCODE -ne 0) { throw 'Could not check the local model installation.' }
    $status = ($output -join "`n") | ConvertFrom-Json
    $selectedDevice = $status.device
    if (-not (Test-CurrentGame $emulator $session)) { exit 0 }
    if (-not $status.installed) {
        Write-Host 'First-run setup: downloading/configuring Kev for local gameplay.'
        Write-Host 'The model is 2.54 GB, plus runtime files. Verified files are reused; interrupted downloads resume.'
        & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'serve.ps1') -Device $selectedDevice -DownloadOnly
        if ($LASTEXITCODE -ne 0) { throw 'Local model setup failed. Click Enable local model to retry.' }
    } else { Write-Host "Kev model verified; $selectedDevice runtime is installed." }
    if (-not (Test-CurrentGame $emulator $session)) { exit 0 }
    $nodeOutput = & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'ensure-node.ps1')
    if ($LASTEXITCODE -ne 0) {
        $nodeOutput | ForEach-Object { Write-Host $_ }
        throw 'The local player runtime could not be configured.'
    }
    # A child PowerShell writes setup progress to stdout as well as its final path.
    $nodeLines = @($nodeOutput)
    if ($nodeLines.Count -gt 1) { $nodeLines[0..($nodeLines.Count - 2)] | ForEach-Object { Write-Host $_ } }
    $node = ($nodeLines | Select-Object -Last 1).Trim()
    if (-not (Test-CurrentGame $emulator $session)) { exit 0 }

    $baseUrl = if ($env:KEV_BASE_URL) { $env:KEV_BASE_URL } else { 'http://127.0.0.1:8008' }
    $uri = [Uri]$baseUrl
    if (-not $uri.IsAbsoluteUri -or $uri.Scheme -ne 'http' -or
        $uri.Host -notin @('127.0.0.1', 'localhost', '[::1]') -or $uri.AbsolutePath -ne '/' -or
        $uri.UserInfo -or $uri.Query -or $uri.Fragment) { throw 'KEV_BASE_URL must be a local HTTP origin.' }
    $headers = @{}
    if ($env:KEV_API_KEY) { $headers.Authorization = "Bearer $env:KEV_API_KEY" }
    function Test-KevReady {
        try {
            $models = Invoke-RestMethod -Uri ($baseUrl.TrimEnd('/') + '/v1/models') -Headers $headers -TimeoutSec 2 -MaximumRedirection 0
        } catch {
            if ($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -in @(401, 403)) {
                throw 'Kev rejected authentication. Check KEV_API_KEY.'
            }
            return $false
        }
        if (-not @($models.data | Where-Object { $_.id -eq 'kev-latest' }).Count) {
            throw "A different server is listening at $baseUrl. Change KEV_BASE_URL or close that server."
        }
        return $true
    }
    if (-not (Test-KevReady)) {
        if ($uri.Host -eq '[::1]') { throw 'Use KEV_BASE_URL=http://127.0.0.1:8008 for automatic startup.' }
        Write-Host 'Starting Kev at your request...'
        $arguments = '-NoLogo -NoProfile -ExecutionPolicy Bypass -File "' +
            (Join-Path $PSScriptRoot 'serve.ps1') + '" -Device ' + $selectedDevice + ' -Port ' + $uri.Port
        $ownedServer = Start-Process -FilePath $powershell -ArgumentList $arguments -PassThru
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $ready = $false
        while ($timer.Elapsed.TotalSeconds -lt 180 -and (Test-CurrentGame $emulator $session)) {
            if ($ownedServer.HasExited) { throw 'Kev exited during startup. Check its console or retry with -Device cpu.' }
            if (Test-KevReady) { $ready = $true; break }
            Start-Sleep -Seconds 1
        }
        if (-not (Test-CurrentGame $emulator $session)) { exit 0 }
        if (-not $ready) { throw 'Kev did not become ready within three minutes. Check its console.' }
    } else { Write-Host 'Reusing the ready local Kev server.' }
    if (-not (Test-CurrentGame $emulator $session)) { exit 0 }
    if ($initialGoal) { Write-Host 'Kev is ready. Starting the preset goal you selected in mGBA.' }
    else { Write-Host 'Kev is ready. Select a preset when you want autonomous gameplay; Enter keeps manual control.' }
    Write-Host 'Current gameplay adapter: supported English Emerald only; other games stop without blind input.'
    Write-Host 'Save in-game and disconnect other agents before starting. Disable local model in mGBA to stop.'
    Write-Host 'Continuous run budget: 10,000 iterations / 12 hours, with route memory and animation waits.'
    if ($env:LUNA_HELP -eq '1') {
        Write-Host 'GPT-6 Luna recovery requested: up to 20 paid calls sharing decoded state. API credentials/model ID are required.'
    } else { Write-Host 'GPT-6 Luna recovery is off. Configure LUNA_API_KEY/LUNA_MODEL before opening mGBA to enable the dialog option.' }
    $goal = $initialGoal
    while (Test-CurrentGame $emulator $session) {
        if (-not $goal) { $goal = Read-GameGoal $emulator $session $presets }
        if ([string]::IsNullOrWhiteSpace($goal) -or -not (Test-CurrentGame $emulator $session)) { break }
        & $node (Join-Path $PSScriptRoot 'cli.mjs') --session $SessionId --game-id $GameId --goal $goal
        $goal = ''
        Write-Host 'Player stopped. Inspect/save the game before selecting another goal.'
    }
    Write-Host 'Manual control remains available. Disable local model in mGBA when finished.'
    while (Test-CurrentGame $emulator $session) { Start-Sleep -Seconds 1 }
} catch {
    Write-Host "Local AI: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host 'mGBA remains usable. Click Enable local model to retry setup.'
    exit 1
} finally {
    # Only terminate the server tree started by this helper, never a reused server.
    if ($ownedServer -and -not $ownedServer.HasExited) {
        & "$env:SystemRoot\System32\taskkill.exe" /PID $ownedServer.Id /T /F 2>$null | Out-Null
    }
    if ($lock) { $lock.Dispose() }
}
