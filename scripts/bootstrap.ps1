[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$UpdateYtDlp
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root = Split-Path -Parent $PSScriptRoot
$ThirdParty = Join-Path $Root 'third_party'
$Downloads = Join-Path $ThirdParty 'downloads'
$VlcRoot = Join-Path $ThirdParty 'vlc'
$YtDlpPath = Join-Path $ThirdParty 'yt-dlp.exe'
$VlcVersion = '3.0.21'
$VlcArchive = Join-Path $Downloads "vlc-$VlcVersion-win64.zip"
$VlcUrl = "https://download.videolan.org/pub/videolan/vlc/$VlcVersion/win64/vlc-$VlcVersion-win64.zip"
$VlcSha256 = 'A0B7EC02B50ADF6417EED014FB8DF50AF39690505A4225B85B3DC2ED17D14843'
$YtDlpUrl = 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe'
$YtDlpChecksumsUrl = 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS'

New-Item -ItemType Directory -Force -Path $ThirdParty, $Downloads | Out-Null

function Download-File {
    param([string]$Uri, [string]$Destination)
    Write-Host "Downloading $Uri"
    $temporary = "$Destination.partial"
    Remove-Item $temporary -Force -ErrorAction SilentlyContinue
    Invoke-WebRequest -Uri $Uri -OutFile $temporary -UseBasicParsing
    Move-Item $temporary $Destination -Force
}

$VlcReady = (Test-Path (Join-Path $VlcRoot 'libvlc.dll')) -and
    (Test-Path (Join-Path $VlcRoot 'libvlccore.dll')) -and
    (Test-Path (Join-Path $VlcRoot 'plugins'))

if ($Force -or -not $VlcReady) {
    if ($Force -or -not (Test-Path $VlcArchive)) {
        Download-File -Uri $VlcUrl -Destination $VlcArchive
    }
    $actualVlcHash = (Get-FileHash $VlcArchive -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualVlcHash -ne $VlcSha256) {
        Remove-Item $VlcArchive -Force
        throw "VLC checksum mismatch. Expected $VlcSha256, got $actualVlcHash"
    }
    Write-Host "Extracting VLC $VlcVersion runtime..."
    $extractRoot = Join-Path $Downloads 'vlc-extract'
    Remove-Item $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $VlcRoot -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -Path $VlcArchive -DestinationPath $extractRoot -Force
    $inner = Join-Path $extractRoot "vlc-$VlcVersion"
    if (-not (Test-Path $inner)) {
        $inner = (Get-ChildItem $extractRoot -Directory | Select-Object -First 1).FullName
    }
    Move-Item $inner $VlcRoot
    Remove-Item $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($Force -or $UpdateYtDlp -or -not (Test-Path $YtDlpPath)) {
    Download-File -Uri $YtDlpUrl -Destination $YtDlpPath
    Write-Host 'Verifying yt-dlp SHA-256...'
    $checksumContent = (Invoke-WebRequest -Uri $YtDlpChecksumsUrl -UseBasicParsing).Content
    $checksumText = if ($checksumContent -is [byte[]]) {
        [Text.Encoding]::UTF8.GetString($checksumContent)
    } else {
        [string]$checksumContent
    }
    $checksumLine = ($checksumText -split "`n" | Where-Object { $_ -match '\syt-dlp\.exe\s*$' } | Select-Object -First 1)
    if (-not $checksumLine) {
        Remove-Item $YtDlpPath -Force
        throw 'yt-dlp checksum entry was not found.'
    }
    $expected = ($checksumLine -split '\s+')[0].ToUpperInvariant()
    $actual = (Get-FileHash $YtDlpPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $expected) {
        Remove-Item $YtDlpPath -Force
        throw "yt-dlp checksum mismatch. Expected $expected, got $actual"
    }
}

$VlcReady = (Test-Path (Join-Path $VlcRoot 'libvlc.dll')) -and
    (Test-Path (Join-Path $VlcRoot 'libvlccore.dll')) -and
    (Test-Path (Join-Path $VlcRoot 'plugins'))
if (-not $VlcReady) {
    throw 'VLC archive layout is incomplete; expected DLLs and plugins.'
}
if (-not (Test-Path $YtDlpPath)) {
    throw 'yt-dlp.exe is missing.'
}

Write-Host ''
Write-Host 'Dependencies are ready:' -ForegroundColor Green
Write-Host "  VLC:    $VlcRoot"
Write-Host "  yt-dlp: $YtDlpPath"
