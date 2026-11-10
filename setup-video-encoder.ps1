param([string]$Destination = (Join-Path $PSScriptRoot 'video-tools'))
$ErrorActionPreference = 'Stop'
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$existing = Get-ChildItem -LiteralPath $Destination -Filter ffmpeg.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($existing) { Write-Host "Choose FFmpeg in the pause panel, then select: $($existing.FullName)"; exit 0 }
$downloadRoot = Join-Path $Destination '.downloads'
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
$archive = Join-Path $downloadRoot 'ffmpeg-release-essentials.zip'
$checksumFile = Join-Path $downloadRoot 'ffmpeg-release-essentials.zip.sha256'
Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile $archive
Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip.sha256' -OutFile $checksumFile
$expected = [regex]::Match((Get-Content -LiteralPath $checksumFile -Raw), '[a-fA-F0-9]{64}').Value
if (-not $expected -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'Encoder checksum verification failed; nothing will be executed.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$installFolder = Join-Path $Destination ('install-' + [Guid]::NewGuid().ToString('N'))
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $installFolder)
$encoder = Get-ChildItem -LiteralPath $installFolder -Filter ffmpeg.exe -Recurse | Select-Object -First 1
if (-not $encoder) { throw 'The verified archive did not contain ffmpeg.exe.' }
Write-Host 'Encoder downloaded and verified. It is a separate GPL-licensed program, not bundled into the mod.'
Write-Host "Open pause > Frame Meter > Choose FFmpeg and select: $($encoder.FullName)"
