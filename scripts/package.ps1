# Builds Kayra and packs dist\kayra-<version>-windows-x64.zip.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& build\bin\kayra-cli.exe selftest
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$version = (Select-String CMakeLists.txt -Pattern 'project\(Kayra VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$stage = Join-Path $root "dist\kayra"
Remove-Item -Recurse -Force (Join-Path $root "dist") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item build\bin\kayra.exe, build\bin\kayra-cli.exe, LICENSE, README.md $stage
$zip = Join-Path $root "dist\kayra-$version-windows-x64.zip"
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host $zip
