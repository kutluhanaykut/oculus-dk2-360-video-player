[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildOutput = Join-Path $Root "build\bin\$Configuration"
$ThirdParty = Join-Path $Root 'third_party'
$StageRoot = Join-Path $Root 'dist\DK2-360-VR-Player'
$Archive = Join-Path $Root 'dist\DK2-360-VR-Player-win64.zip'
$Exe = Join-Path $BuildOutput 'DK2VRPlayer.exe'

if (-not (Test-Path $Exe)) {
    throw "Executable not found: $Exe. Run scripts/build.ps1 first."
}

Remove-Item $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $Archive -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

Copy-Item $Exe $StageRoot
Copy-Item (Join-Path $ThirdParty 'vlc\libvlc.dll') $StageRoot
Copy-Item (Join-Path $ThirdParty 'vlc\libvlccore.dll') $StageRoot
Copy-Item (Join-Path $ThirdParty 'vlc\plugins') (Join-Path $StageRoot 'plugins') -Recurse
Copy-Item (Join-Path $ThirdParty 'yt-dlp.exe') $StageRoot
Copy-Item (Join-Path $Root 'README.md') $StageRoot
Copy-Item (Join-Path $Root 'LICENSE') $StageRoot
Copy-Item (Join-Path $Root 'THIRD_PARTY_NOTICES.md') $StageRoot

$VlcLicenseCandidates = @(
    (Join-Path $ThirdParty 'vlc\COPYING.txt'),
    (Join-Path $ThirdParty 'vlc\COPYING'),
    (Join-Path $ThirdParty 'vlc\copyright')
)
foreach ($candidate in $VlcLicenseCandidates) {
    if (Test-Path $candidate) {
        Copy-Item $candidate (Join-Path $StageRoot ('VLC-' + (Split-Path $candidate -Leaf)))
    }
}

Compress-Archive -Path $StageRoot -DestinationPath $Archive -CompressionLevel Optimal
Write-Host "Portable package: $Archive" -ForegroundColor Green
