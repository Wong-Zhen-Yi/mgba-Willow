# Install and register this checkout's local MCP without changing model settings.
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & npm.cmd ci
    if ($LASTEXITCODE -ne 0) { throw 'MCP dependency installation failed.' }
    $nodePath = (Get-Command node.exe -ErrorAction Stop).Source
    & codex.cmd mcp add mgba-willow -- $nodePath (Join-Path $PSScriptRoot 'server.mjs')
    if ($LASTEXITCODE -ne 0) { throw 'Codex MCP registration failed.' }
    Write-Host 'mGBA MCP registered. Restart the Codex chat to load its tools, choose GPT-6 Luna / medium, and open a ROM with Launch mGBA.cmd.'
} finally { Pop-Location }
