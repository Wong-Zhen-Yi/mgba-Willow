# Read-only discovery: opening a pipe and requesting session metadata never enables AI control.
function Get-LoadedGame {
    param([int]$EmulatorPid)
    $directory = Join-Path $env:LOCALAPPDATA 'mgba-willow\ai\sessions'
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -Filter '*.json' -File -ErrorAction SilentlyContinue)) {
        $pipe = $null
        try {
            $descriptor = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            if ($descriptor.pid -ne $EmulatorPid -or $descriptor.protocol -ne 1 -or
                $descriptor.pipe -notmatch '^mgba-willow-ai-[0-9a-f-]{36}$') { continue }
            $pipe = New-Object IO.Pipes.NamedPipeClientStream('.', $descriptor.pipe, [IO.Pipes.PipeDirection]::InOut)
            $pipe.Connect(300)
            $writer = New-Object IO.StreamWriter($pipe)
            $writer.AutoFlush = $true
            $reader = New-Object IO.StreamReader($pipe)
            $writer.WriteLine('{"id":1,"method":"session"}')
            $response = $reader.ReadLineAsync()
            if (-not $response.Wait(1500)) { continue }
            $message = $response.Result | ConvertFrom-Json
            $session = $message.result
            if ($message.id -eq 1 -and -not $message.error -and $session.started -and
                $session.session_id -eq $descriptor.session_id -and $session.game_id -match '^[0-9a-f-]{36}$') {
                return $session
            }
        } catch { # Stale descriptors, disabled MCP and games changing during discovery are normal.
        } finally {
            if ($pipe) { $pipe.Dispose() }
        }
    }
    return $null
}

function Test-EmulatorAlive {
    param([Diagnostics.Process]$Emulator)
    try { return -not $Emulator.HasExited } catch { return $false }
}

function Test-CurrentGame {
    param([Diagnostics.Process]$Emulator, $Session)
    if (-not (Test-EmulatorAlive $Emulator)) { return $false }
    $current = Get-LoadedGame $Emulator.Id
    return $null -ne $current -and $current.session_id -eq $Session.session_id -and $current.game_id -eq $Session.game_id
}
