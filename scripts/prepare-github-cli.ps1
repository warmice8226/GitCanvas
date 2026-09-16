param([string]$Version = '2.100.0')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$cache = Join-Path $projectRoot "build\github-cli-$Version"
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$name = "gh_${Version}_windows_amd64.zip"
$archive = Join-Path $cache $name
$checksums = Join-Path $cache "gh_${Version}_checksums.txt"
$release = "https://github.com/cli/cli/releases/download/v$Version"
if (!(Test-Path -LiteralPath $checksums)) { Invoke-WebRequest -UseBasicParsing -Uri "$release/gh_${Version}_checksums.txt" -OutFile $checksums }
if (!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest -UseBasicParsing -Uri "$release/$name" -OutFile $archive }
$entry = Get-Content -LiteralPath $checksums | Where-Object { $_ -match ("^[a-fA-F0-9]{64}\s+" + [regex]::Escape($name) + '$') }
if (@($entry).Count -ne 1) { throw 'GitHub CLI checksum entry missing or ambiguous' }
$expected = ($entry -split '\s+')[0]
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'GitHub CLI checksum mismatch' }
Expand-Archive -LiteralPath $archive -DestinationPath $cache -Force
if (!(Test-Path -LiteralPath "$cache\bin\gh.exe")) { throw 'GitHub CLI executable missing' }
Write-Host "Verified GitHub CLI: $cache\bin\gh.exe"
