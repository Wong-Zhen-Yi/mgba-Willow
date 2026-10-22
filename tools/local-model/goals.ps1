# Shared preset catalog used by the native picker and the console's optional numbered menu.
function Get-GameplayPresets {
    param([string]$Path = (Join-Path $PSScriptRoot 'goals.json'))
    $file = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($file.Length -gt 65536) { throw 'Gameplay goal presets are too large.' }
    $catalog = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    $goals = @($catalog.goals)
    if ($catalog.version -ne 1 -or $goals.Count -lt 1 -or $goals.Count -gt 32) {
        throw 'Gameplay goal presets have an invalid format.'
    }
    $ids = @{}
    foreach ($goal in $goals) {
        if ($goal.id -isnot [string] -or $goal.id -cnotmatch '^[a-z][a-z0-9-]{0,47}$' -or $ids.ContainsKey($goal.id) -or
            $goal.label -isnot [string] -or [string]::IsNullOrWhiteSpace($goal.label) -or $goal.label.Length -gt 80 -or
            $goal.goal -isnot [string] -or [string]::IsNullOrWhiteSpace($goal.goal) -or $goal.goal.Length -gt 512) {
            throw 'Gameplay goal presets contain an invalid or duplicate entry.'
        }
        $ids[$goal.id] = $true
    }
    return $goals
}

function Resolve-GameplayPreset {
    param($Presets, [string]$GoalId)
    $selected = @($Presets | Where-Object { $_.id -ceq $GoalId })
    if ($selected.Count -ne 1) { throw "Unknown gameplay goal preset: $GoalId" }
    return $selected[0].goal
}
