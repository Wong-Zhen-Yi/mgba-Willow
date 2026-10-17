param(
    [string]$PythonExecutable = "python",
    [switch]$Register
)

$ErrorActionPreference = 'Stop'
$venvDirectory = Join-Path $PSScriptRoot '.venv'
& $PythonExecutable -m venv $venvDirectory
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or newer is required.' }
$venvPython = Join-Path $venvDirectory 'Scripts\python.exe'
& $venvPython -m pip install -e $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Python MCP dependency installation failed.' }

if ($Register) {
    & codex.cmd mcp add mgba-willow-python -- $venvPython -m mgba_willow
    if ($LASTEXITCODE -ne 0) { throw 'Codex MCP registration failed.' }
    Write-Host 'Registered mgba-willow-python. Restart the Codex chat to load its tools.'
} else {
    Write-Host 'Installed the Python controller. Run setup.ps1 -Register to register its MCP server.'
}
