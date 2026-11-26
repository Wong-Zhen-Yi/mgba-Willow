# Register the checkout's terminal launcher for the current Windows user.
$ErrorActionPreference = 'Stop'
$terminalDir = Join-Path $PSScriptRoot 'terminal'
$launcher = Join-Path $terminalDir 'mgba.cmd'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Terminal launcher is missing: $launcher"
}

$existing = Get-Command mgba -ErrorAction SilentlyContinue
if ($existing -and $existing.Path -ne $launcher) {
    throw "mgba already resolves to $($existing.Definition). Remove that command from PATH before registering Willow."
}

# Read the raw value so existing entries such as %USERPROFILE% stay expandable.
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
try {
    $userPath = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $entries = @(if ($userPath) {
        $userPath -split ';' | Where-Object {
            [Environment]::ExpandEnvironmentVariables($_).Trim('"').TrimEnd('\', '/') -ne $terminalDir
        }
    })
    $newPath = (@($terminalDir) + $entries) -join ';'
    if ($userPath -ceq $newPath) {
        Write-Host 'The mgba command is already registered for this checkout.'
        return
    }
    $kind = if ($null -ne $key.GetValue('Path')) { $key.GetValueKind('Path') } else { 'ExpandString' }
    $key.SetValue('Path', $newPath, $kind)
} finally {
    $key.Dispose()
}

# Tell Explorer to refresh the environment used for newly opened terminals.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class WillowTerminalEnvironment {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr window, uint message, UIntPtr wParam, string text, uint flags, uint timeout, out UIntPtr result);
}
'@
$result = [UIntPtr]::Zero
[void][WillowTerminalEnvironment]::SendMessageTimeout(
    [IntPtr]0xffff, 0x001a, [UIntPtr]::Zero, 'Environment', 0x0002, 1000, [ref]$result)
Write-Host 'The mgba command is registered. Open a new terminal window to use it.'
