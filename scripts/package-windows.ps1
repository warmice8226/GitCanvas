param([string]$QtRoot = 'C:\msys64\ucrt64')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$env:PATH = "$QtRoot\bin;" + $env:PATH
$buildRoot = Join-Path $projectRoot 'build'
$stageRoot = Join-Path $projectRoot ('out\stage-' + [guid]::NewGuid().ToString('N'))
$packageRoot = Join-Path $stageRoot 'GitCanvas'
& "$PSScriptRoot\prepare-github-cli.ps1"
& "$QtRoot\bin\cmake.exe" -S $projectRoot -B $buildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Configure failed' }
& "$QtRoot\bin\cmake.exe" --build $buildRoot --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
& "$QtRoot\bin\ctest.exe" --test-dir $buildRoot --output-on-failure
if ($LASTEXITCODE) {
    if (Test-Path -LiteralPath "$buildRoot\prototype-tests.txt") { Get-Content -LiteralPath "$buildRoot\prototype-tests.txt" -Tail 80 }
    throw 'Tests failed'
}
& "$QtRoot\bin\cmake.exe" --install $buildRoot --prefix $packageRoot
if ($LASTEXITCODE) { throw 'Deployment failed' }
# Keep the headless platform plugin for the deployed startup check.
Copy-Item -LiteralPath "$QtRoot\share\qt6\plugins\platforms\qoffscreen.dll" -Destination "$packageRoot\share\qt6\plugins\platforms\qoffscreen.dll" -Force
# windeployqt does not collect all transitive MSYS2/MinGW runtime libraries.
$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if ($seen.ContainsKey($binary.ToLowerInvariant())) { continue }
    $seen[$binary.ToLowerInvariant()] = $true
    $imports = & "$QtRoot\bin\objdump.exe" -p $binary
    if ($LASTEXITCODE) { throw "Dependency scan failed: $binary" }
    foreach ($line in $imports) {
        if ($line -match 'DLL Name:\s*(.+)$') {
            $name = $Matches[1].Trim()
            $source = Join-Path "$QtRoot\bin" $name
            $target = Join-Path "$packageRoot\bin" $name
            if (Test-Path -LiteralPath $source) {
                if (!$seen.ContainsKey($target.ToLowerInvariant())) { Copy-Item -LiteralPath $source -Destination $target -Force }
                $queue.Enqueue($target)
            } elseif (!(Test-Path -LiteralPath "$env:SystemRoot\System32\$name") -and $name -notmatch '^(api-ms-|ext-ms-)') {
                throw "Unresolved runtime dependency: $name ($binary)"
            }
        }
    }
}
Copy-Item -LiteralPath "$projectRoot\LICENSE" -Destination $packageRoot
Copy-Item -LiteralPath "$projectRoot\docs\WINDOWS_PROTOTYPE.md" -Destination "$packageRoot\README.md"
Copy-Item -LiteralPath "$projectRoot\THIRD_PARTY_NOTICES.md" -Destination $packageRoot
Copy-Item -LiteralPath "$projectRoot\docs\USER_GUIDE.html" -Destination $packageRoot
New-Item -ItemType Directory -Force "$packageRoot\third-party-licenses" | Out-Null
Copy-Item -LiteralPath "$buildRoot\github-cli-2.100.0\bin\gh.exe" -Destination "$packageRoot\bin\gh.exe" -Force
Copy-Item -LiteralPath "$buildRoot\github-cli-2.100.0\LICENSE" -Destination "$packageRoot\third-party-licenses\GitHub-CLI-LICENSE" -Force
Get-ChildItem -LiteralPath "$QtRoot\share\licenses" | Copy-Item -Destination "$packageRoot\third-party-licenses" -Recurse -Force
$zip = Join-Path $projectRoot 'out\GitCanvas-0.1.0-windows-x64.zip'
# Test the deployed application with only OS utilities and its own DLL directory.
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new("$packageRoot\bin\GitCanvas.exe", '--smoke-test -platform offscreen')
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $smoke = [System.Diagnostics.Process]::Start($startInfo)
    try {
        if (!$smoke.WaitForExit(20000)) { $smoke.Kill(); throw 'Portable startup timed out' }
        if ($smoke.ExitCode -ne 0) { throw "Portable startup failed: $($smoke.ExitCode)" }
    } finally { $smoke.Dispose() }
} finally { $env:PATH = $savedPath }
@(& "$QtRoot\bin\cmake.exe" --version; & "$QtRoot\bin\g++.exe" --version; & git --version; & "$QtRoot\bin\qmake6.exe" -query QT_VERSION) | Set-Content -LiteralPath "$packageRoot\build-environment.txt" -Encoding UTF8
$pacman = Join-Path (Split-Path $QtRoot -Parent) 'usr\bin\pacman.exe'
if (!(Test-Path -LiteralPath $pacman)) { throw 'MSYS2 package inventory tool missing' }
& $pacman -Q | Set-Content -LiteralPath "$packageRoot\build-packages.txt" -Encoding UTF8
if ($LASTEXITCODE) { throw 'MSYS2 package inventory failed' }
$dependencySources = Join-Path $projectRoot 'out\windows-dependency-sources'
& "$QtRoot\bin\python.exe" "$PSScriptRoot\collect-windows-sources.py" --qt-root $QtRoot --output $dependencySources --package $packageRoot
if ($LASTEXITCODE) { throw 'Matching dependency sources / signature verification failed' }
Copy-Item -LiteralPath "$dependencySources\SOURCES.json" -Destination "$packageRoot\dependency-sources.json" -Force
& "$QtRoot\bin\python.exe" "$PSScriptRoot\portable-metadata.py" --package $packageRoot --sources "$projectRoot\out\GitCanvas-0.1.0-source.zip"
if ($LASTEXITCODE) { throw 'Source archive / inventory generation failed' }
Compress-Archive -Path $packageRoot -DestinationPath $zip -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path $zip -Leaf)`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "Portable package: $zip"
