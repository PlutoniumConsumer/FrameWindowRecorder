param(
    [string]$SdkPath = $env:GEODE_SDK,
    [string]$CMake = 'cmake',
    [string]$GeodeCli = 'geode',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
if (-not $SdkPath) { throw 'Pass -SdkPath or set GEODE_SDK to the Geode v5.10.1 SDK.' }
$env:GEODE_SDK = (Resolve-Path -LiteralPath $SdkPath).Path
$cliPath = (Get-Command $GeodeCli -ErrorAction Stop).Source
& $CMake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DGEODE_CLI=$cliPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMake --build $BuildDirectory --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Mod compilation/package creation failed.' }
Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.geode'
