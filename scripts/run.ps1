[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Exe = Join-Path $Root "build\bin\$Configuration\DK2VRPlayer.exe"

if (-not (Test-Path $Exe)) {
    throw "Executable not found: $Exe. Run scripts/build.ps1 first."
}

Write-Host "Starting: $Exe" -ForegroundColor Green
& $Exe
