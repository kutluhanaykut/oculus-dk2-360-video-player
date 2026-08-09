[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$SkipBootstrap,
    [switch]$Clean,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $Root 'build'

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot 'bootstrap.ps1')
}
if ($Clean -and (Test-Path $BuildDirectory)) {
    Write-Host "Removing $BuildDirectory"
    Remove-Item $BuildDirectory -Recurse -Force
}

$CmakeArguments = @(
    '-S', $Root,
    '-B', $BuildDirectory,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    '-DDK2VR_BUILD_TESTS=ON',
    '-DDK2VR_PACKAGE_VLC_PLUGINS=OFF'
)
& cmake @CmakeArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

if ($Package) {
    & (Join-Path $PSScriptRoot 'package.ps1') -Configuration $Configuration
}

$Exe = Join-Path $BuildDirectory "bin\$Configuration\DK2VRPlayer.exe"
Write-Host ''
Write-Host "Build complete: $Exe" -ForegroundColor Green
